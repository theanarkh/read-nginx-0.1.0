
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_http.h>


static char *ngx_http_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_merge_locations(ngx_conf_t *cf,
                                      ngx_array_t *locations,
                                      void **loc_conf,
                                      ngx_http_module_t *module,
                                      ngx_uint_t ctx_index);

int         ngx_http_max_module;

ngx_uint_t  ngx_http_total_requests;
uint64_t    ngx_http_total_sent;


ngx_int_t  (*ngx_http_top_header_filter) (ngx_http_request_t *r);
ngx_int_t  (*ngx_http_top_body_filter) (ngx_http_request_t *r, ngx_chain_t *ch);


static ngx_command_t  ngx_http_commands[] = {

    {ngx_string("http"),
     NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
     ngx_http_block,
     0,
     0,
     NULL},

    ngx_null_command
};

    
static ngx_core_module_t  ngx_http_module_ctx = {
    ngx_string("http"),
    NULL,
    NULL
};  

// http模块是nginx的核心模块
ngx_module_t  ngx_http_module = {
    NGX_MODULE,
    &ngx_http_module_ctx,                  /* module context */
    ngx_http_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init module */
    NULL                                   /* init child */
};


static char *ngx_http_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                        *rv;
    ngx_uint_t                   mi, m, s, l, p, a, n;
    ngx_uint_t                   port_found, addr_found, virtual_names;
    ngx_conf_t                   pcf;
    ngx_array_t                  in_ports;
    ngx_listening_t             *ls;
    ngx_http_listen_t           *lscf;
    ngx_http_module_t           *module;
    ngx_http_handler_pt         *h;
    ngx_http_conf_ctx_t         *ctx;
    ngx_http_in_port_t          *in_port, *inport;
    ngx_http_in_addr_t          *in_addr, *inaddr;
    ngx_http_server_name_t      *s_name, *name;
    ngx_http_core_srv_conf_t   **cscfp, *cscf;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_main_conf_t   *cmcf;
#if (WIN32)
    ngx_iocp_conf_t             *iocpcf;
#endif

    /* the main http context */
    ngx_test_null(ctx,
                  ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t)),
                  NGX_CONF_ERROR);
    // 创建一个ngx_http_conf_ctx_t结构体挂载到cycle的ctx中
    *(ngx_http_conf_ctx_t **) conf = ctx;

    /* count the number of the http modules and set up their indices */

    ngx_http_max_module = 0;
    // 遍历所有的nginx模块，筛选出http模块
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        // 标记该模块在http中的索引，位置
        ngx_modules[m]->ctx_index = ngx_http_max_module++;
    }
    // 根据http的模块数，分配一个数组，分别由下面三个字段指向
    /* the main http main_conf, it's the same in the all http contexts */
    ngx_test_null(ctx->main_conf,
                  ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module),
                  NGX_CONF_ERROR);

    /* the http null srv_conf, it's used to merge the server{}s' srv_conf's */
    ngx_test_null(ctx->srv_conf,
                  ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module),
                  NGX_CONF_ERROR);

    /* the http null loc_conf, it's used to merge the server{}s' loc_conf's */
    ngx_test_null(ctx->loc_conf,
                  ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module),
                  NGX_CONF_ERROR);


    /* create the main_conf, srv_conf and loc_conf in all http modules */

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        // http模块的上下文，是ngx_http_module_t结构体
        module = ngx_modules[m]->ctx;
        mi = ngx_modules[m]->ctx_index;
        // 解析http配置前执行的钩子
        if (module->pre_conf) {
            if (module->pre_conf(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
        // 创建一个ngx_http_core_main_conf_t结构体，挂到main_conf数组相应的位置
        if (module->create_main_conf) {
            ngx_test_null(ctx->main_conf[mi], module->create_main_conf(cf),
                          NGX_CONF_ERROR);
        }
        // 创建一个ngx_http_core_srv_conf_t结构体，挂到srv_conf数组相应的位置
        if (module->create_srv_conf) {
            ngx_test_null(ctx->srv_conf[mi], module->create_srv_conf(cf), 
                          NGX_CONF_ERROR);
        }
        // 创建一个ngx_http_core_loc_conf_s 结构体，挂到loc_conf数组相应的位置
        if (module->create_loc_conf) {
            ngx_test_null(ctx->loc_conf[mi], module->create_loc_conf(cf),
                          NGX_CONF_ERROR);
        }
    }

    /* parse inside the http{} block */

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_HTTP_MODULE;
    cf->cmd_type = NGX_HTTP_MAIN_CONF;
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        *cf = pcf;
        return rv;
    }

    /*
     * init http{} main_conf's, merge the server{}s' srv_conf's
     * and its location{}s' loc_conf's
     */
    // 取出ngx_http_core_module在http上下文的main配置 
    cmcf = ctx->main_conf[ngx_http_core_module.ctx_index];
    // server指令的管理结构
    cscfp = cmcf->servers.elts;

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = ngx_modules[m]->ctx;
        mi = ngx_modules[m]->ctx_index;

        /* init http{} main_conf's */
        // ngx_conf_parse没有赋值的字段在这进行初始化
        if (module->init_main_conf) {
            rv = module->init_main_conf(cf, ctx->main_conf[mi]);
            if (rv != NGX_CONF_OK) {
                *cf = pcf;
                return rv;
            }
        }
        // http上下文下所有的server指令对应的结构体数组
        for (s = 0; s < cmcf->servers.nelts; s++) {

            /* merge the server{}s' srv_conf's */

            if (module->merge_srv_conf) {
                // 合并http层和server层配置
                rv = module->merge_srv_conf(cf,
                                            // 子模块在http上下文的srv_conf配置，内容由子模块创建
                                            ctx->srv_conf[mi],
                                            /*
                                                cscfp[s]指向server上下文中的ngx_http_core_srv_conf_t结构体，
                                                通过反向指针找到server指令创建的上下文，再找出某个模块的的srv_conf
                                            */
                                            cscfp[s]->ctx->srv_conf[mi]);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }
            }

            if (module->merge_loc_conf) {

                /* merge the server{}'s loc_conf */
                // 合并http层和非嵌套的location层的配置
                rv = module->merge_loc_conf(cf,
                                            ctx->loc_conf[mi],
                                            cscfp[s]->ctx->loc_conf[mi]);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }

                /* merge the locations{}' loc_conf's */
                /*
                    合并server和非嵌套location的配置、非嵌套location和嵌套location的配置,
                    &cscfp[s]->locations：server下的所有非嵌套location
                    cscfp[s]->ctx->loc_conf，server层各模块关于location的配置 
                */
                rv = ngx_http_merge_locations(cf, &cscfp[s]->locations,
                                              cscfp[s]->ctx->loc_conf,
                                              module, mi);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }

#if 0
                clcfp = (ngx_http_core_loc_conf_t **) cscfp[s]->locations.elts;

                for (l = 0; l < cscfp[s]->locations.nelts; l++) {
                    rv = module->merge_loc_conf(cf,
                                                cscfp[s]->ctx->loc_conf[mi],
                                                clcfp[l]->loc_conf[mi]);
                    if (rv != NGX_CONF_OK) {
                        *cf = pcf;
                        return rv;
                    }
                }
#endif
            }
        }
    }

    /* we needed "http"'s cf->ctx while merging configuration */
    // 恢复上下文
    *cf = pcf;

    /* init lists of the handlers */

    ngx_init_array(cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers,
                   cf->cycle->pool, 10, sizeof(ngx_http_handler_pt),
                   NGX_CONF_ERROR);
    cmcf->phases[NGX_HTTP_REWRITE_PHASE].type = NGX_OK;


    /* the special find config phase for single handler */

    ngx_init_array(cmcf->phases[NGX_HTTP_FIND_CONFIG_PHASE].handlers,
                   cf->cycle->pool, 1, sizeof(ngx_http_handler_pt),
                   NGX_CONF_ERROR);
    cmcf->phases[NGX_HTTP_FIND_CONFIG_PHASE].type = NGX_OK;

    ngx_test_null(h, ngx_push_array(
                           &cmcf->phases[NGX_HTTP_FIND_CONFIG_PHASE].handlers),
                  NGX_CONF_ERROR);
    *h = ngx_http_find_location_config;


    ngx_init_array(cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers,
                   cf->cycle->pool, 10, sizeof(ngx_http_handler_pt),
                   NGX_CONF_ERROR);
    cmcf->phases[NGX_HTTP_ACCESS_PHASE].type = NGX_DECLINED;


    ngx_init_array(cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers,
                   cf->cycle->pool, 10, sizeof(ngx_http_handler_pt),
                   NGX_CONF_ERROR);
    cmcf->phases[NGX_HTTP_CONTENT_PHASE].type = NGX_OK;


    /*
     * create the lists of the ports, the addresses and the server names
     * to allow quickly find the server core module configuration at run-time
     */

    ngx_init_array(in_ports, cf->pool, 10, sizeof(ngx_http_in_port_t),
                   NGX_CONF_ERROR);

    /* "server" directives */
    cscfp = cmcf->servers.elts;
    // 遍历每个server下的每个listen结构的每个端口，构造端口->地址->servername的多级结构
    for (s = 0; s < cmcf->servers.nelts; s++) {

        /* "listen" directives */
        // 在解析listen指令的时候写入
        lscf = cscfp[s]->listen.elts;
        for (l = 0; l < cscfp[s]->listen.nelts; l++) {

            port_found = 0;

            /* AF_INET only */

            in_port = in_ports.elts;
            for (p = 0; p < in_ports.nelts; p++) {

                if (lscf[l].port == in_port[p].port) {
                    // 端口已经在监听
                    /* the port is already in the port list */

                    port_found = 1;
                    addr_found = 0;
                    // 监听了这个端口的地址列表
                    in_addr = in_port[p].addrs.elts;
                    for (a = 0; a < in_port[p].addrs.nelts; a++) {
                        // 监听的端口相等，判断地址是否也相等
                        if (lscf[l].addr == in_addr[a].addr) {

                            /* the address is already bound to this port */

                            /* "server_name" directives */
                            // server对应的servername列表
                            s_name = cscfp[s]->server_names.elts;
                            for (n = 0; n < cscfp[s]->server_names.nelts; n++) {

                                /*
                                 * add the server name and server core module
                                 * configuration to the address:port
                                 */

                                /* TODO: duplicate names can be checked here */

                                ngx_test_null(name,
                                              ngx_push_array(&in_addr[a].names),
                                              NGX_CONF_ERROR);

                                name->name = s_name[n].name;
                                name->core_srv_conf = s_name[n].core_srv_conf;
                            }

                            /*
                             * check duplicate "default" server that
                             * serves this address:port
                             */
                            // 同一个server配置里只能有一个default_server
                            if (lscf[l].default_server) {
                                if (in_addr[a].default_server) {
                                    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                                           "duplicate default server in %s:%d",
                                           lscf[l].file_name.data,
                                           lscf[l].line);

                                    return NGX_CONF_ERROR;
                                }

                                in_addr[a].core_srv_conf = cscfp[s];
                                in_addr[a].default_server = 1;
                            }

                            addr_found = 1;

                            break;

                        } else if (in_addr[a].addr == INADDR_ANY) {

                            /*
                             * "*:port" must be the last resort so move it
                             * to the end of the address list and add
                             * the new address at its place
                             */
                            /*
                                如果监听的地址是any，则把any的项放到地址列表的最后，
                                原来的位置填充当前的listen配置的信息,遍历到any项说明
                                已经是地址列表的最后一项
                            */
                            ngx_test_null(inaddr,
                                          ngx_push_array(&in_port[p].addrs),
                                          NGX_CONF_ERROR);

                            ngx_memcpy(inaddr, &in_addr[a],
                                       sizeof(ngx_http_in_addr_t));

                            in_addr[a].addr = lscf[l].addr;
                            in_addr[a].default_server = lscf[l].default_server;
                            in_addr[a].core_srv_conf = cscfp[s];

                            /*
                             * create the empty list of the server names that
                             * can be served on this address:port
                             */

                            ngx_init_array(inaddr->names, cf->pool, 10,
                                           sizeof(ngx_http_server_name_t),
                                           NGX_CONF_ERROR);
                            // 置1，下面不需要再新增一个项
                            addr_found = 1;
                            // 跳出循环，因为已经遍历到了any项了，说明是最后一项了
                            break;
                        }
                    }
                    // 还没有该地址的项，新增一个
                    if (!addr_found) {

                        /*
                         * add the address to the addresses list that
                         * bound to this port
                         */

                        ngx_test_null(inaddr,
                                      ngx_push_array(&in_port[p].addrs),
                                      NGX_CONF_ERROR);

                        inaddr->addr = lscf[l].addr;
                        inaddr->default_server = lscf[l].default_server;
                        inaddr->core_srv_conf = cscfp[s];

                        /*
                         * create the empty list of the server names that
                         * can be served on this address:port
                         */

                        ngx_init_array(inaddr->names, cf->pool, 10,
                                       sizeof(ngx_http_server_name_t),
                                       NGX_CONF_ERROR);
                    }
                }
            }
            // 在已监听端口里没有找到当前端口
            if (!port_found) {

                /* add the port to the in_port list */
                // 记录该端口
                ngx_test_null(in_port,
                              ngx_push_array(&in_ports),
                              NGX_CONF_ERROR);

                in_port->port = lscf[l].port;

                ngx_test_null(in_port->port_text.data, ngx_palloc(cf->pool, 7),
                              NGX_CONF_ERROR);
                in_port->port_text.len = ngx_snprintf((char *)
                                                      in_port->port_text.data,
                                                      7, ":%d",
                                                      in_port->port);

                /* create list of the addresses that bound to this port ... */
                // 监听了这个端口的地址列表
                ngx_init_array(in_port->addrs, cf->pool, 10,
                               sizeof(ngx_http_in_addr_t),
                               NGX_CONF_ERROR);

                ngx_test_null(inaddr, ngx_push_array(&in_port->addrs),
                              NGX_CONF_ERROR);

                /* ... and add the address to this list */

                inaddr->addr = lscf[l].addr;
                inaddr->default_server = lscf[l].default_server;
                // 记录server配置，方便后续快速查找
                inaddr->core_srv_conf = cscfp[s];

                /*
                 * create the empty list of the server names that
                 * can be served on this address:port
                 */

                ngx_init_array(inaddr->names, cf->pool, 10,
                               sizeof(ngx_http_server_name_t),
                               NGX_CONF_ERROR);
            }
        }
    }

    /* optimize the lists of the ports, the addresses and the server names */

    /* AF_INET only */
    // 优化，如果不存在虚拟主机则不需要保存servername，因为servername是用来匹配虚拟主机的
    // 监听的端口列表
    in_port = in_ports.elts;
    for (p = 0; p < in_ports.nelts; p++) {

        /* check whether the all server names point to the same server */
        // 地址列表
        in_addr = in_port[p].addrs.elts;
        for (a = 0; a < in_port[p].addrs.nelts; a++) {

            virtual_names = 0;
            // servername列表
            name = in_addr[a].names.elts;
            for (n = 0; n < in_addr[a].names.nelts; n++) {
                /*
                    比较当前的地址和该地址下所有的servername，如果指向的server配置
                    是一样的，说明当前的地址收到连接或者请求的时候，直接给server里配置的服务
                    就行，如果有一个servername指向的server配置和当前地址的不一样，说明当一个请求或
                    连接到来时，nginx无法通过地址决定该请求转发给哪个服务，还需要servername信息，即
                    虚拟主机名
                */
                if (in_addr[a].core_srv_conf != name[n].core_srv_conf) {
                    virtual_names = 1;
                    break;
                }
            }

            /*
             * if the all server names point to the same server
             * then we do not need to check them at run-time
             */
            // 没有配置虚拟主机则不需要保存servername信息
            if (!virtual_names) {
                in_addr[a].names.nelts = 0;
            }
        }

        /*
         * if there's the binding to "*:port" then we need to bind()
         * to "*:port" only and ignore the other bindings
         */
        // 地址列表的最后一项是不是any，如果绑定的地址中有any，则绑定该地址即可，其他的地址不需要绑定了
        if (in_addr[a - 1].addr == INADDR_ANY) {
            a--;

        } else {
            a = 0;
        }

        in_addr = in_port[p].addrs.elts;
        while (a < in_port[p].addrs.nelts) {
            // push到cycle->listening中，并填充某些字段
            ls = ngx_listening_inet_stream_socket(cf, in_addr[a].addr,
                                                  in_port[p].port);
            if (ls == NULL) {
                return NGX_CONF_ERROR;
            }

            ls->backlog = -1;
#if 0
#if 0
            ls->nonblocking = 1;
#else
            ls->nonblocking = 0;
#endif
#endif
            ls->addr_ntop = 1;

            ls->handler = ngx_http_init_connection;
            // 地址对应的server配置
            cscf = in_addr[a].core_srv_conf;
            ls->pool_size = cscf->connection_pool_size;
            ls->post_accept_timeout = cscf->post_accept_timeout;

            clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
            ls->log = clcf->err_log;

#if (WIN32)
            iocpcf = ngx_event_get_conf(cf->cycle->conf_ctx, ngx_iocp_module);
            if (iocpcf->acceptex_read) {
                ls->post_accept_buffer_size = cscf->client_header_buffer_size;
            }
#endif

            ls->ctx = ctx;
            // 有多个地址并且没有监听any地址
            if (in_port[p].addrs.nelts > 1) {

                in_addr = in_port[p].addrs.elts;
                // 没有绑定any的配置
                if (in_addr[in_port[p].addrs.nelts - 1].addr != INADDR_ANY) {

                    /*
                     * if this port has not the "*:port" binding then create
                     * the separate ngx_http_in_port_t for the all bindings
                     */
                    /*
                        分配一个新的端口和地址结构，形成一个端口对应一个地址的结构，
                        但是servername是共享的，比如端口1*地址数+端口2*地址数个结构体
                    */
                    ngx_test_null(inport,
                                  ngx_palloc(cf->pool,
                                             sizeof(ngx_http_in_port_t)),
                                  NGX_CONF_ERROR);
                    // 复制
                    inport->port = in_port[p].port;
                    inport->port_text = in_port[p].port_text;

                    /* init list of the addresses ... */

                    ngx_init_array(inport->addrs, cf->pool, 1,
                                   sizeof(ngx_http_in_addr_t),
                                   NGX_CONF_ERROR);

                    /* ... and set up it with the first address */
                    // 地址等于当前循环的地址
                    inport->addrs.nelts = 1;
                    inport->addrs.elts = in_port[p].addrs.elts;

                    ls->servers = inport;

                    /* prepare for the next cycle */
                    // 指向地址列表中下一个地址
                    in_port[p].addrs.elts = (char *) in_port[p].addrs.elts
                                                       + in_port[p].addrs.size;
                    in_port[p].addrs.nelts--;

                    in_addr = (ngx_http_in_addr_t *) in_port[p].addrs.elts;
                    a = 0;

                    continue;
                }
            }

            ls->servers = &in_port[p];
            a++;
        }
    }

#if (NGX_DEBUG)
    in_port = in_ports.elts;
    for (p = 0; p < in_ports.nelts; p++) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                      "port: %d %08x", in_port[p].port, &in_port[p]);
        in_addr = in_port[p].addrs.elts;
        for (a = 0; a < in_port[p].addrs.nelts; a++) {
            u_char ip[20];
            ngx_inet_ntop(AF_INET, &in_addr[a].addr, ip, 20);
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                           "%s %08x", ip, in_addr[a].core_srv_conf);
            s_name = in_addr[a].names.elts;
            for (n = 0; n < in_addr[a].names.nelts; n++) {
                 ngx_log_debug2(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                                "%s %08x", s_name[n].name.data,
                                s_name[n].core_srv_conf);
            }
        }
    }
#endif

    return NGX_CONF_OK;
}


static char *ngx_http_merge_locations(ngx_conf_t *cf,
                                      ngx_array_t *locations,
                                      void **loc_conf,
                                      ngx_http_module_t *module,
                                      ngx_uint_t ctx_index)
{
    char                       *rv;
    ngx_uint_t                  i;
    ngx_http_core_loc_conf_t  **clcfp;

    clcfp = /* (ngx_http_core_loc_conf_t **) */ locations->elts;
    // 遍历父层的location
    for (i = 0; i < locations->nelts; i++) {
        /* 
            先合并server和非嵌套location层的配置
            loc_conf[ctx_index]:server层的location配置,
            clcfp[i]->loc_conf[ctx_index]:location层各模块的配置
        */
        rv = module->merge_loc_conf(cf, loc_conf[ctx_index],
                                    clcfp[i]->loc_conf[ctx_index]);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
        /*
          再合并嵌套location和非嵌套location的配置, 
          &clcfp[i]->locations：各模块关于嵌套的lcaotion，
          clcfp[i]->loc_conf：各模块和关于非嵌套location的配置
        */
        rv = ngx_http_merge_locations(cf, &clcfp[i]->locations,
                                      clcfp[i]->loc_conf, module, ctx_index);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
    }

    return NGX_CONF_OK;
}
