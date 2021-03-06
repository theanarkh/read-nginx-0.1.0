
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>


static ngx_int_t ngx_add_inherited_sockets(ngx_cycle_t *cycle);
static ngx_int_t ngx_getopt(ngx_master_ctx_t *ctx, ngx_cycle_t *cycle);
static void *ngx_core_module_create_conf(ngx_cycle_t *cycle);
static char *ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf);
static char *ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_command_t  ngx_core_commands[] = {

    { ngx_string("daemon"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, daemon),
      NULL },

    { ngx_string("master_process"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, master),
      NULL },

    { ngx_string("worker_processes"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_core_conf_t, worker_processes),
      NULL },

#if (NGX_THREADS)

    { ngx_string("worker_threads"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_core_conf_t, worker_threads),
      NULL },

    { ngx_string("thread_stack_size"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      0,
      offsetof(ngx_core_conf_t, thread_stack_size),
      NULL },

#endif

    { ngx_string("user"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE12,
      ngx_set_user,
      0,
      0,
      NULL },

    { ngx_string("pid"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, pid),
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_core_module_ctx = {
    ngx_string("core"),
    ngx_core_module_create_conf,
    ngx_core_module_init_conf
};

// nginx的核心模块，nginx模块分为core、http、log、event四种类型的模块
ngx_module_t  ngx_core_module = {
    NGX_MODULE,
    &ngx_core_module_ctx,                  /* module context */
    ngx_core_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init module */
    NULL                                   /* init process */
};


ngx_uint_t  ngx_max_module;


int main(int argc, char *const *argv)
{
    ngx_int_t          i;
    ngx_log_t         *log;
    ngx_cycle_t       *cycle, init_cycle;
    ngx_core_conf_t   *ccf;
    ngx_master_ctx_t   ctx;

#if defined __FreeBSD__
    ngx_debug_init();
#endif

    /* TODO */ ngx_max_sockets = -1;

    ngx_time_init();

#if (HAVE_PCRE)
    ngx_regex_init();
#endif
    // 获取当前进程的pid
    ngx_pid = ngx_getpid();
    // 初始化错误日志输出结构
    if (!(log = ngx_log_init_stderr())) {
        return 1;
    }

#if (NGX_OPENSSL)
    ngx_ssl_init(log);
#endif

    /* init_cycle->log is required for signal handlers and ngx_getopt() */

    ngx_memzero(&init_cycle, sizeof(ngx_cycle_t));
    init_cycle.log = log;
    ngx_cycle = &init_cycle;
    // 保存执行nginx的命令行参数
    ngx_memzero(&ctx, sizeof(ngx_master_ctx_t));
    ctx.argc = argc;
    ctx.argv = argv;
    // 初始化内存池
    if (!(init_cycle.pool = ngx_create_pool(1024, log))) {
        return 1;
    }
    // 解析执行nginx命令时传入的参数，把值写入init_cycle
    if (ngx_getopt(&ctx, &init_cycle) == NGX_ERROR) {
        return 1;
    }

    if (ngx_test_config) {
        log->log_level = NGX_LOG_INFO;
    }

    if (ngx_os_init(log) == NGX_ERROR) {
        return 1;
    }
    // 继承上一次的打开的socket,存在listen字段表示的数组里，并填充其地址、协议簇等字段
    if (ngx_add_inherited_sockets(&init_cycle) == NGX_ERROR) {
        return 1;
    }
    // 给所有的nginx模块编号
    ngx_max_module = 0;
    for (i = 0; ngx_modules[i]; i++) {
        ngx_modules[i]->index = ngx_max_module++;
    }

    cycle = ngx_init_cycle(&init_cycle);
    if (cycle == NULL) {
        if (ngx_test_config) {
            ngx_log_error(NGX_LOG_EMERG, log, 0,
                          "the configuration file %s test failed",
                          init_cycle.conf_file.data);
        }

        return 1;
    }

    if (ngx_test_config) {
        ngx_log_error(NGX_LOG_INFO, log, 0,
                      "the configuration file %s was tested successfully",
                      init_cycle.conf_file.data);
        return 0;
    }

    ngx_os_status(cycle->log);

    ngx_cycle = cycle;
    // 从解析配置的结构体中获取核心模块的配置
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    // nginx的执行方式，单进程或多进程
    ngx_process = ccf->master ? NGX_PROCESS_MASTER : NGX_PROCESS_SINGLE;

#if (WIN32)

#if 0

    TODO:

    if (ccf->run_as_service) {
        if (ngx_service(cycle->log) == NGX_ERROR) {
            return 1;
        }

        return 0;
    }
#endif

#else
    // 是否已守护进程方式运行
    if (!ngx_inherited && ccf->daemon) {
        if (ngx_daemon(cycle->log) == NGX_ERROR) {
            return 1;
        }

        ngx_daemonized = 1;
    }

    if (ngx_create_pidfile(cycle, NULL) == NGX_ERROR) {
        return 1;
    }

#endif

    if (ngx_process == NGX_PROCESS_MASTER) {
        ngx_master_process_cycle(cycle, &ctx);

    } else {
        ngx_single_process_cycle(cycle, &ctx);
    }

    return 0;
}

// 继承socket，遍历已存在的socket，然后取出每个socket对应的信息，保存在listening结构体中
static ngx_int_t ngx_add_inherited_sockets(ngx_cycle_t *cycle)
{
    u_char              *p, *v, *inherited;
    ngx_socket_t         s;
    ngx_listening_t     *ls;

    inherited = (u_char *) getenv(NGINX_VAR);

    if (inherited == NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                  "using inherited sockets from \"%s\"", inherited);
    // 分配10个listening结构体
    ngx_init_array(cycle->listening, cycle->pool,
                   10, sizeof(ngx_listening_t), NGX_ERROR);

    for (p = inherited, v = p; *p; p++) {
        // 以:或;分割
        if (*p == ':' || *p == ';') {
            // 把字符串转成数字，比如'111'转成111
            s = ngx_atoi(v, p - v);
            if (s == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "invalid socket number \"%s\" in "
                              NGINX_VAR " enviroment variable, "
                              "ignoring the rest of the variable", v);
                break;
            }
            // 指向剩下待解析的字符串首地址
            v = p + 1;
            // 拿到可用元素的首地址
            if (!(ls = ngx_push_array(&cycle->listening))) {
                return NGX_ERROR;
            }
            // 保存之前的fd
            ls->fd = s;
        }
    }
    // 打标记 
    ngx_inherited = 1;

    return ngx_set_inherited_sockets(cycle);
}


ngx_pid_t ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv)
{
    char             *env[2], *var, *p;
    ngx_uint_t        i;
    ngx_pid_t         pid;
    ngx_exec_ctx_t    ctx;
    ngx_listening_t  *ls;

    ctx.path = argv[0];
    ctx.name = "new binary process";
    ctx.argv = argv;

    var = ngx_alloc(sizeof(NGINX_VAR)
                            + cycle->listening.nelts * (NGX_INT32_LEN + 1) + 2,
                    cycle->log);

    p = (char *) ngx_cpymem(var, NGINX_VAR "=", sizeof(NGINX_VAR));

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        p += ngx_snprintf(p, NGX_INT32_LEN + 2, "%u;", ls[i].fd);
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0, "inherited: %s", var);

    env[0] = var;
    env[1] = NULL;
    ctx.envp = (char *const *) &env;

    pid = ngx_execute(cycle, &ctx);

    ngx_free(var);

    return pid;
}


static ngx_int_t ngx_getopt(ngx_master_ctx_t *ctx, ngx_cycle_t *cycle)
{
    ngx_int_t  i;
    // 第一个值是nginx 
    for (i = 1; i < ctx->argc; i++) {
        // 参数要以-开头
        if (ctx->argv[i][0] != '-') {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "invalid option: \"%s\"", ctx->argv[i]);
            return NGX_ERROR;
        }

        switch (ctx->argv[i][1]) {

        case 't':
            ngx_test_config = 1;
            break;
        // 自定义配置文件路径和名字
        case 'c':
            if (ctx->argv[i + 1] == NULL) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "the option: \"%s\" requires file name",
                              ctx->argv[i]);
                return NGX_ERROR;
            }
            // 保存配置文件路径
            cycle->conf_file.data = (u_char *) ctx->argv[++i];
            cycle->conf_file.len = ngx_strlen(cycle->conf_file.data);
            break;

        default:
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "invalid option: \"%s\"", ctx->argv[i]);
            return NGX_ERROR;
        }
    }
    // 没有自定义的配置文件，则取默认的
    if (cycle->conf_file.data == NULL) {
        cycle->conf_file.len = sizeof(NGX_CONF_PATH) - 1;
        cycle->conf_file.data = (u_char *) NGX_CONF_PATH;
    }
    // 解析配置文件的绝对路径
    if (ngx_conf_full_name(cycle, &cycle->conf_file) == NGX_ERROR) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

// 创建一个结构体保存核心配置，并初始化各字段
static void *ngx_core_module_create_conf(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;

    if (!(ccf = ngx_pcalloc(cycle->pool, sizeof(ngx_core_conf_t)))) {
        return NULL;
    }
    /* set by pcalloc()
     *
     * ccf->pid = NULL;
     * ccf->newpid = NULL;
     */
    ccf->daemon = NGX_CONF_UNSET;
    ccf->master = NGX_CONF_UNSET;
    ccf->worker_processes = NGX_CONF_UNSET;
#if (NGX_THREADS)
    ccf->worker_threads = NGX_CONF_UNSET;
    ccf->thread_stack_size = NGX_CONF_UNSET;
#endif
    ccf->user = (ngx_uid_t) NGX_CONF_UNSET;
    ccf->group = (ngx_gid_t) NGX_CONF_UNSET;

    return ccf;
}

// 设置核心模块变量的值
static char *ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

#if !(WIN32)
    struct passwd    *pwd;
    struct group     *grp;
#endif

    ngx_conf_init_value(ccf->daemon, 1);
    ngx_conf_init_value(ccf->master, 1);
    ngx_conf_init_value(ccf->worker_processes, 1);

#if (NGX_THREADS)
    ngx_conf_init_value(ccf->worker_threads, 0);
    ngx_threads_n = ccf->worker_threads;
    ngx_conf_init_size_value(ccf->thread_stack_size, 2 * 1024 * 1024);
#endif

#if !(WIN32)

    if (ccf->user == (uid_t) NGX_CONF_UNSET) {
        // struct passwd {
        //     char * pw_name; /* Username. */
        //     char * pw_passwd; /* Password. */
        //     __uid_t pw_uid; /* User ID. */
        //     __gid_t pw_gid; /* Group ID. */
        //     char * pw_gecos; /* Real name. */
        //     char * pw_dir; /* Home directory. -*/
        //     char * pw_shell; /* Shell program. */
        // };
        pwd = getpwnam("nobody");
        if (pwd == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getpwnam(\"nobody\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->user = pwd->pw_uid;

        grp = getgrnam("nobody");
        if (grp == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getgrnam(\"nobody\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->group = grp->gr_gid;
    }

    if (ccf->pid.len == 0) {
        ccf->pid.len = sizeof(NGX_PID_PATH) - 1;
        ccf->pid.data = NGX_PID_PATH;
    }

    if (ngx_conf_full_name(cycle, &ccf->pid) == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    ccf->newpid.len = ccf->pid.len + sizeof(NGX_NEWPID_EXT);

    if (!(ccf->newpid.data = ngx_palloc(cycle->pool, ccf->newpid.len))) {
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(ngx_cpymem(ccf->newpid.data, ccf->pid.data, ccf->pid.len),
               NGX_NEWPID_EXT, sizeof(NGX_NEWPID_EXT));

#endif

    return NGX_CONF_OK;
}


static char *ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (WIN32)

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"user\" is not supported, ignored");

    return NGX_CONF_OK;

#else

    ngx_core_conf_t  *ccf = conf;

    struct passwd    *pwd;
    struct group     *grp;
    ngx_str_t        *value;

    if (ccf->user != (uid_t) NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = (ngx_str_t *) cf->args->elts;

    pwd = getpwnam((const char *) value[1].data);
    if (pwd == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getpwnam(%s) failed", value[1].data);
        return NGX_CONF_ERROR;
    }

    ccf->user = pwd->pw_uid;

    if (cf->args->nelts == 2) {
        return NGX_CONF_OK;
    }

    grp = getgrnam((const char *) value[2].data);
    if (grp == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getgrnam(%s) failed", value[1].data);
        return NGX_CONF_ERROR;
    }

    ccf->group = grp->gr_gid;

    return NGX_CONF_OK;

#endif
}
