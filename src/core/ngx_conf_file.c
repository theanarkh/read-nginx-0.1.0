
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


static char *ngx_conf_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_command_t  ngx_conf_commands[] = {

    { ngx_string("include"),
      NGX_ANY_CONF|NGX_CONF_TAKE1,
      ngx_conf_include,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_module_t  ngx_conf_module = {
    NGX_MODULE,
    NULL,                                  /* module context */
    ngx_conf_commands,                     /* module directives */
    NGX_CONF_MODULE,                       /* module type */
    NULL,                                  /* init module */
    NULL                                   /* init child */
};



/* The ten fixed arguments */

static int argument_number[] = {
    NGX_CONF_NOARGS,
    NGX_CONF_TAKE1,
    NGX_CONF_TAKE2,
    NGX_CONF_TAKE3,
    NGX_CONF_TAKE4,
    NGX_CONF_TAKE5,
    NGX_CONF_TAKE6,
    NGX_CONF_TAKE7
};

static int ngx_conf_read_token(ngx_conf_t *cf);

// 解析配置函数
char *ngx_conf_parse(ngx_conf_t *cf, ngx_str_t *filename)
{
    int               m, rc, found, valid;
    char             *rv;
    void             *conf, **confp;
    ngx_fd_t          fd;
    ngx_str_t        *name;
    ngx_conf_file_t  *prev;
    ngx_command_t    *cmd;

#if (NGX_SUPPRESS_WARN)
    fd = NGX_INVALID_FILE;
    prev = NULL;
#endif
    // 
    if (filename) {

        /* open configuration file */
        // 打开配置文件
        fd = ngx_open_file(filename->data, NGX_FILE_RDONLY, NGX_FILE_OPEN);
        if (fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
                          ngx_open_file_n " %s failed", filename->data);
            return NGX_CONF_ERROR;
        }

        prev = cf->conf_file;
        // 分配一个新的结构体，保存文件的信息
        if (!(cf->conf_file = ngx_palloc(cf->pool, sizeof(ngx_conf_file_t)))) {
            return NGX_CONF_ERROR;
        }
        // 获取文件的stat信息，写入info字段
        if (ngx_fd_info(fd, &cf->conf_file->file.info) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
                          ngx_fd_info_n " %s failed", filename->data);
        }
        // 分配一块由buffer管理的内存给分析文件时用
        if (!(cf->conf_file->buffer = ngx_create_temp_buf(cf->pool, 1024))) {
            return NGX_CONF_ERROR;
        }

        cf->conf_file->file.fd = fd;
        cf->conf_file->file.name.len = filename->len;
        cf->conf_file->file.name.data = filename->data;
        cf->conf_file->file.offset = 0;
        cf->conf_file->file.log = cf->log;;
        cf->conf_file->line = 1;
    }

    for ( ;; ) {
        rc = ngx_conf_read_token(cf);

        /*
         * ngx_conf_read_token() returns NGX_OK, NGX_ERROR,
         * NGX_CONF_FILE_DONE or NGX_CONF_BLOCK_DONE
         */

#if 0
ngx_log_debug(cf->log, "token %d" _ rc);
#endif

        if (rc == NGX_ERROR) {
            break;
        }

        if (rc != NGX_OK) {
            break;
        }

        if (cf->handler) {

            /* custom handler, i.e. used in http "types { ... }" directive */

            rv = (*cf->handler)(cf, NULL, cf->handler_conf);
            if (rv == NGX_CONF_OK) {
                continue;

            } else if (rv == NGX_CONF_ERROR) {
                rc = NGX_ERROR;
                break;

            } else {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                             "%s in %s:%d",
                             rv,
                             cf->conf_file->file.name.data,
                             cf->conf_file->line);
                rc = NGX_ERROR;
                break;
            }
        }
        // 在ngx_conf_read_token里赋值
        name = (ngx_str_t *) cf->args->elts;
        found = 0;
        // 把解析到的参数分到每个匹配的模块中处理
        for (m = 0; rc != NGX_ERROR && !found && ngx_modules[m]; m++) {

            /* look up the directive in the appropriate modules */
            // cf->module_type保存了当前的模块类型
            if (ngx_modules[m]->type != NGX_CONF_MODULE
                && ngx_modules[m]->type != cf->module_type)
            {
                continue;
            }

            cmd = ngx_modules[m]->commands;
            if (cmd == NULL) {
                continue;
            }
            // 匹配到当前命令
            while (cmd->name.len) {
                if (name->len == cmd->name.len
                    && ngx_strcmp(name->data, cmd->name.data) == 0)
                {

                    found = 1;
#if 0
ngx_log_debug(cf->log, "command '%s'" _ cmd->name.data);
#endif
                    /* is the directive's location right ? */

                    if ((cmd->type & cf->cmd_type) == 0) {
                        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                      "directive \"%s\" in %s:%d "
                                      "is not allowed here",
                                      name->data,
                                      cf->conf_file->file.name.data,
                                      cf->conf_file->line);
                        rc = NGX_ERROR;
                        break;
                    }

                    /* is the directive's argument count right ? */

                    if (cmd->type & NGX_CONF_ANY) {
                        valid = 1;

                    } else if (cmd->type & NGX_CONF_FLAG) {

                        if (cf->args->nelts == 2) {
                            valid = 1;
                        } else {
                            valid = 0;
                        }

                    } else if (cmd->type & NGX_CONF_1MORE) {

                        if (cf->args->nelts > 1) {
                            valid = 1;
                        } else {
                            valid = 0;
                        }

                    } else if (cmd->type & NGX_CONF_2MORE) {

                        if (cf->args->nelts > 2) {
                            valid = 1;
                        } else {
                            valid = 0;
                        }

                    } else if (cf->args->nelts <= 10
                               && (cmd->type
                                   & argument_number[cf->args->nelts - 1]))
                    {
                        valid = 1;

                    } else {
                        valid = 0;
                    }

                    if (!valid) {
                        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                      "invalid number arguments in "
                                      "directive \"%s\" in %s:%d",
                                      name->data,
                                      cf->conf_file->file.name.data,
                                      cf->conf_file->line);
                        rc = NGX_ERROR;
                        break;
                    }

                    /* set up the directive's configuration context */

                    conf = NULL;
                    // 可以出现在配置文件中最外层。只有ngx_core_module_ctx模块才是这种类型
                    if (cmd->type & NGX_DIRECT_CONF) {
                        // 二级指针
                        conf = ((void **) cf->ctx)[ngx_modules[m]->index];

                    } else if (cmd->type & NGX_MAIN_CONF) {
                        // conf是四级指针
                        conf = &(((void **) cf->ctx)[ngx_modules[m]->index]);

                    } else if (cf->ctx) { // 指向当前模块下的配置数组
                        // 一级指针
                        confp = *(void **) ((char *) cf->ctx + cmd->conf);

                        if (confp) {
                            conf = confp[ngx_modules[m]->ctx_index];
                        }
                    }

                    rv = cmd->set(cf, cmd, conf);

#if 0
ngx_log_debug(cf->log, "rv: %d" _ rv);
#endif

                    if (rv == NGX_CONF_OK) {
                        break;

                    } else if (rv == NGX_CONF_ERROR) {
                        rc = NGX_ERROR;
                        break;

                    } else {
                        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                      "the \"%s\" directive %s in %s:%d",
                                      name->data, rv,
                                      cf->conf_file->file.name.data,
                                      cf->conf_file->line);

                        rc = NGX_ERROR;
                        break;
                    }
                }

                cmd++;
            }
        }
        // 该指令没有匹配到任何模块
        if (!found) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "unknown directive \"%s\" in %s:%d",
                          name->data,
                          cf->conf_file->file.name.data,
                          cf->conf_file->line);

            rc = NGX_ERROR;
            break;
        }

        if (rc == NGX_ERROR) {
            break;
        }
    }

    if (filename) {
        // 保存原来的文件信息
        cf->conf_file = prev;
        // 关闭打开的配置文件
        if (ngx_close_file(fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                          ngx_close_file_n " %s failed",
                          cf->conf_file->file.name.data);
            return NGX_CONF_ERROR;
        }
    }

    if (rc == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static int ngx_conf_read_token(ngx_conf_t *cf)
{
    u_char      *start, ch, *src, *dst;
    int          len;
    int          found, need_space, last_space, sharp_comment;
    int          quoted, s_quoted, d_quoted;
    ssize_t      n;
    ngx_str_t   *word;
    ngx_buf_t   *b;

    found = 0;
    need_space = 0;
    last_space = 1;
    sharp_comment = 0;
    quoted = s_quoted = d_quoted = 0;

    cf->args->nelts = 0;
    b = cf->conf_file->buffer;
    start = b->pos;

#if 0
ngx_log_debug(cf->log, "TOKEN START");
#endif

    for ( ;; ) {

        if (b->pos >= b->last) {
            if (cf->conf_file->file.offset
                                 >= ngx_file_size(&cf->conf_file->file.info)) {
                return NGX_CONF_FILE_DONE;
            }

            if (b->pos - start) {
                ngx_memcpy(b->start, start, b->pos - start);
            }

            n = ngx_read_file(&cf->conf_file->file,
                              b->start + (b->pos - start),
                              b->end - (b->start + (b->pos - start)),
                              cf->conf_file->file.offset);

            if (n == NGX_ERROR) {
                return NGX_ERROR;
            }

            b->pos = b->start + (b->pos - start);
            start = b->start;
            b->last = b->pos + n;
        }

        ch = *b->pos++;

#if 0
ngx_log_debug(cf->log, "%d:%d:%d:%d:%d '%c'" _
              last_space _ need_space _
              quoted _ s_quoted _ d_quoted _ ch);
#endif
        // 如果遇到换行，则行数加1
        if (ch == LF) {
            cf->conf_file->line++;
            // 处于注释状态，因为换行了，则清0，结束注释状态
            if (sharp_comment) {
                sharp_comment = 0;
            }
        }
        // 如果是注释状态，则忽略当前字符，直到换行
        if (sharp_comment) {
            continue;
        }
        // 如果当前是转义状态，则得到后面一个字符后清0
        if (quoted) {
            quoted = 0;
            continue;
        }
        // 需要空格，并且当前字符是空格，则清0，置当前状态为空格状态
        if (need_space) {
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                last_space = 1;
                need_space = 0;
                continue;
            }

            if (ch == ';' || ch == '{') {
                return NGX_OK;
            }

            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "unexpected '%c' in %s:%d",
                          ch, cf->conf_file->file.name.data,
                          cf->conf_file->line);

            return NGX_ERROR;
        }
        
        if (last_space) {
            // 当前是空格状态，并且当前字符是空格，则忽略
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                continue;
            }

            start = b->pos - 1;

            switch (ch) {

            case ';':
            case '{':
            // 遇到块开始字符，后续却没有参数则报错
                if (cf->args->nelts == 0) {
                    ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                  "unexpected '%c' in %s:%d",
                                  ch, cf->conf_file->file.name.data,
                                  cf->conf_file->line);
                    return NGX_ERROR;
                }

                return NGX_OK;

            case '}':
            // 遇到块结束字符，后续还有参数则报错，否则块结束
                if (cf->args->nelts > 0) {
                    ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                                  "unexpected '}' in %s:%d",
                                  cf->conf_file->file.name.data,
                                  cf->conf_file->line);
                    return NGX_ERROR;
                }

                return NGX_CONF_BLOCK_DONE;
            // 遇到#则标记当前状态为注释
            case '#':
                sharp_comment = 1;
                continue;
            // 置转义状态
            case '\\':
                quoted = 1;
                last_space = 0;
                continue;
            // 置双引号状态
            case '"':
                start++;
                d_quoted = 1;
                last_space = 0;
                continue;
            // 置单引号状态
            case '\'':
                start++;
                s_quoted = 1;
                last_space = 0;
                continue;

            default:
                last_space = 0;
            }

        } else {
            if (ch == '\\') {
                quoted = 1;
                continue;
            }
            // 双引号状态遇到结束符则清0，后续需要跟空格
            if (d_quoted) {
                if (ch == '"') {
                    d_quoted = 0;
                    need_space = 1;
                    found = 1;
                }

            } else if (s_quoted) {
                if (ch == '\'') {
                    s_quoted = 0;
                    need_space = 1;
                    found = 1;
                }

            } else if (ch == ' ' || ch == '\t' || ch == CR || ch == LF
                       || ch == ';' || ch == '{') {
                last_space = 1;
                found = 1;
            }

            if (found) {
                if (!(word = ngx_push_array(cf->args))) {
                    return NGX_ERROR;
                }

                if (!(word->data = ngx_palloc(cf->pool, b->pos - start + 1))) {
                    return NGX_ERROR;
                }

                for (dst = word->data, src = start, len = 0;
                     src < b->pos - 1;
                     len++)
                {
                    // 当前是转义符，则前瞻
                    if (*src == '\\') {
                        switch (src[1]) {
                        case '"':
                        case '\'':
                        case '\\':
                            src++;
                            break;

                        case 't':
                            *dst++ = '\t';
                            src += 2;
                            continue;

                        case 'r':
                            *dst++ = '\r';
                            src += 2;
                            continue;

                        case 'n':
                            *dst++ = '\n';
                            src += 2;
                            continue;
                        }

                    }
                    *dst++ = *src++;
                }
                *dst = '\0';
                word->len = len;

#if 0
ngx_log_debug(cf->log, "FOUND %d:'%s'" _ word->len _ word->data);
#endif

                if (ch == ';' || ch == '{') {
                    return NGX_OK;
                }

                found = 0;
            }
        }
    }
}


static char *ngx_conf_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t  *value, file;

    value = cf->args->elts;
    file = value[1];

    if (ngx_conf_full_name(cf->cycle, &file) == NGX_ERROR){
        return NGX_CONF_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

    return ngx_conf_parse(cf, &file);
}


ngx_int_t ngx_conf_full_name(ngx_cycle_t *cycle, ngx_str_t *name)
{
    u_char     *p;
    ngx_str_t   old;
    // 根目录开头则说明已经是绝对路径
    if (name->data[0] == '/') {
        return NGX_OK;
    }
    // 保存相对路径的字符串
    old = *name;
    // 根目录的路径长度加配置文件的相对路径长度=绝对路径长度
    name->len = cycle->root.len + old.len;
    // 分配存储绝对路径的内存
    if (!(name->data = ngx_palloc(cycle->pool, name->len + 1))) {
        return  NGX_ERROR;
    }
    // 把绝对路径复制到name中
    p = ngx_cpymem(name->data, cycle->root.data, cycle->root.len),
    ngx_cpystrn(p, old.data, old.len + 1);

    return NGX_OK;
}


ngx_open_file_t *ngx_conf_open_file(ngx_cycle_t *cycle, ngx_str_t *name)
{
    ngx_str_t         full;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

#if (NGX_SUPPRESS_WARN)
    full.len = 0;
    full.data = NULL;
#endif

    if (name) {
        full = *name;

        if (ngx_conf_full_name(cycle, &full) == NGX_ERROR) {
            return NULL;
        }

        part = &cycle->open_files.part;
        file = part->elts;

        for (i = 0; /* void */ ; i++) {

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                file = part->elts;
                i = 0;
            }

            if (full.len != file[i].name.len) {
                continue;
            }

            if (ngx_strcmp(full.data, file[i].name.data) == 0) {
                return &file[i];
            }
        }
    }
    // nginx初始化的时候，name是null，open_files是空数组
    if (!(file = ngx_list_push(&cycle->open_files))) {
        return NULL;
    }

    if (name) {
        file->fd = NGX_INVALID_FILE;
        file->name = full;

    } else {
        // 默认使用标准错误流作为错误输出
        file->fd = ngx_stderr_fileno;
        file->name.len = 0;
        file->name.data = NULL;
    }

    return file;
}


void ngx_conf_log_error(ngx_uint_t level, ngx_conf_t *cf, ngx_err_t err,
                        char *fmt, ...)
{
    int      len;
    char     errstr[NGX_MAX_CONF_ERRSTR];
    va_list  args;

    va_start(args, fmt);
    len = ngx_vsnprintf(errstr, sizeof(errstr) - 1, fmt, args);
    va_end(args);

    if (err) {
        len += ngx_snprintf(errstr + len, sizeof(errstr) - len - 1,
                            " (%d: ", err);
        len += ngx_strerror_r(err, errstr + len, sizeof(errstr) - len - 1);
        errstr[len++] = ')';
        errstr[len] = '\0';
    }

    ngx_log_error(level, cf->log, 0, "%s in %s:%d",
                  errstr, cf->conf_file->file.name.data, cf->conf_file->line);
}


char *ngx_conf_set_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *value;
    ngx_flag_t       *fp;
    ngx_conf_post_t  *post;

    fp = (ngx_flag_t *) (p + cmd->offset);

    if (*fp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcasecmp(value[1].data, "on") == 0) {
        *fp = 1;

    } else if (ngx_strcasecmp(value[1].data, "off") == 0) {
        *fp = 0;

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                     "invalid value \"%s\" in \"%s\" directive, "
                     "it must be \"on\" or \"off\"",
                     value[1].data, cmd->name.data);
        return NGX_CONF_ERROR;
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, fp);
    }

    return NGX_CONF_OK;
}


char *ngx_conf_set_str_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *field, *value;
    ngx_conf_post_t  *post;

    field = (ngx_str_t *) (p + cmd->offset);

    if (field->data) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *field = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, field);
    }

    return NGX_CONF_OK;
}


char *ngx_conf_set_num_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_int_t        *np;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    np = (ngx_int_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;
    *np = ngx_atoi(value[1].data, value[1].len);
    if (*np == NGX_ERROR) {
        return "invalid number";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, np);
    }

    return NGX_CONF_OK;
}


char *ngx_conf_set_size_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    size_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (size_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_size(&value[1]);
    if (*sp == (size_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *ngx_conf_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_msec_t       *msp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    msp = (ngx_msec_t *) (p + cmd->offset);
    if (*msp != NGX_CONF_UNSET_MSEC) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *msp = ngx_parse_time(&value[1], 0);
    if (*msp == (ngx_msec_t) NGX_ERROR) {
        return "invalid value";
    }

    if (*msp == (ngx_msec_t) NGX_PARSE_LARGE_TIME) {
        return "value must be less than 597 hours";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, msp);
    }

    return NGX_CONF_OK;
}


char *ngx_conf_set_sec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    time_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (time_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_time(&value[1], 1);
    if (*sp == NGX_ERROR) {
        return "invalid value";
    }

    if (*sp == NGX_PARSE_LARGE_TIME) {
        return "value must be less than 68 years";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *ngx_conf_set_bufs_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *p = conf;

    ngx_str_t   *value;
    ngx_bufs_t  *bufs;


    bufs = (ngx_bufs_t *) (p + cmd->offset);
    if (bufs->num) {
        return "is duplicate";
    }

    value = cf->args->elts;

    bufs->num = ngx_atoi(value[1].data, value[1].len);
    if (bufs->num == NGX_ERROR || bufs->num == 0) {
        return "invalid value";
    }

    bufs->size = ngx_parse_size(&value[2]);
    if (bufs->size == (size_t) NGX_ERROR || bufs->size == 0) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


char *ngx_conf_set_enum_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t       *np, i;
    ngx_str_t        *value;
    ngx_conf_enum_t  *e;

    np = (ngx_uint_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    e = cmd->post;

    for (i = 0; e[i].name.len != 0; i++) {
        if (e[i].name.len != value[1].len
            || ngx_strcasecmp(e[i].name.data, value[1].data) != 0)
        {
            continue;
        }

        *np = e[i].value;

        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "invalid value \"%s\"", value[1].data);

    return NGX_CONF_ERROR;
}


char *ngx_conf_set_bitmask_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t          *np, i, m;
    ngx_str_t           *value;
    ngx_conf_bitmask_t  *mask;


    np = (ngx_uint_t *) (p + cmd->offset);
    value = cf->args->elts;
    mask = cmd->post;

    for (i = 1; i < cf->args->nelts; i++) {
        for (m = 0; mask[m].name.len != 0; m++) {

            if (mask[m].name.len != value[i].len
                || ngx_strcasecmp(mask[m].name.data, value[i].data) != 0)
            {
                continue;
            }

            if (*np & mask[m].mask) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate value \"%s\"", value[i].data);

            } else {
                *np |= mask[m].mask;
            }

            break;
        }

        if (mask[m].name.len == 0) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "invalid value \"%s\"", value[i].data);

            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


char *ngx_conf_unsupported(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    return "unsupported on this platform";
}


char *ngx_conf_check_num_bounds(ngx_conf_t *cf, void *post, void *data)
{
    ngx_conf_num_bounds_t  *bounds = post;
    ngx_int_t  *np = data;

    if (bounds->high == -1) {
        if (*np >= bounds->low) {
            return NGX_CONF_OK;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "value must be equal or more than %d", bounds->low);

        return NGX_CONF_ERROR;
    }

    if (*np >= bounds->low && *np <= bounds->high) {
        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "value must be between %d and %d",
                       bounds->low, bounds->high);

    return NGX_CONF_ERROR;
}
