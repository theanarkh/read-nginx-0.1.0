
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


static ngx_uint_t ngx_temp_number;
static ngx_uint_t ngx_random;

// 把chain结构体对应的数据写到临时文件，如果临时文件不存在则创建
int ngx_write_chain_to_temp_file(ngx_temp_file_t *tf, ngx_chain_t *chain)
{
    int  rc;

    if (tf->file.fd == NGX_INVALID_FILE) {
        rc = ngx_create_temp_file(&tf->file, tf->path, tf->pool,
                                  tf->persistent);

        if (rc == NGX_ERROR || rc == NGX_AGAIN) {
            return rc;
        }

        if (!tf->persistent && tf->warn) {
            ngx_log_error(NGX_LOG_WARN, tf->file.log, 0, tf->warn);
        }
    }

    return ngx_write_chain_to_file(&tf->file, chain, tf->offset, tf->pool);
}

// 创建一个临时文件
int ngx_create_temp_file(ngx_file_t *file, ngx_path_t *path,
                         ngx_pool_t *pool, int persistent)
{
    int        num;
    ngx_err_t  err;
    // 文件名长度 = 根目录名字长度 + 一个斜杠 + 多层子目录总长度 + 文件名长度10
    file->name.len = path->name.len + 1 + path->len + 10;
    // +1因为还有'\0'
    ngx_test_null(file->name.data, ngx_palloc(pool, file->name.len + 1),
                  NGX_ERROR);

#if 0
    for (i = 0; i < file->name.len; i++) {
         file->name.data[i] = 'X';
    }
#endif
    // 把根目录的数据先赋值到文件名中
    ngx_memcpy(file->name.data, path->name.data, path->name.len);
    // 获取一个随机数，代表文件名，子目录的名字从该随机数中获取 
    num = ngx_next_temp_number(0);
    /*
        指针移到根目录名字长度 + 一个斜杠 + 多层子目录总长度，准备写入文件名，
        以无符号的格式，写入10位数字，不够10位前面补0，snprintf最多写入10个字符，
        留一个字节写'\0'，所以这里是11
    */
    for ( ;; ) {
        ngx_snprintf((char *)
                            (file->name.data + path->name.len + 1 + path->len),
                     11, "%010u", num);
        // 目录和文件名都有了，子目录名字还没有，在下面这个函数创建
        ngx_create_hashed_filename(file, path);

#if 1
        file->fd = ngx_open_tempfile(file->name.data, persistent);
#else
        file->fd = ngx_open_tempfile(file->name.data, 1);
#endif

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, file->log, 0,
                       "temp fd:%d", file->fd);
        // 如果fd有效，说明该文件已经存在，不需要继续走下面的流程，比如创建子目录
        if (file->fd != NGX_INVALID_FILE) {
            return NGX_OK;
        }

        err = ngx_errno;
        // 该文件已经存在，修改文件名继续执行上面的流程
        if (err == NGX_EEXIST) {
            num = ngx_next_temp_number(1);
            continue;
        }

        if ((path->level[0] == 0)
            || (err != NGX_ENOENT
#if (WIN32)
                && err != NGX_ENOTDIR
#endif
        )) {
            ngx_log_error(NGX_LOG_CRIT, file->log, err,
                          ngx_open_tempfile_n " \"%s\" failed",
                          file->name.data);
            return NGX_ERROR;
        }

        if (ngx_create_path(file, path) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }
}

// 创建子目录的名字
void ngx_create_hashed_filename(ngx_file_t *file, ngx_path_t *path)
{
    int     i, name, pos;
    size_t  level;

    name = file->name.len;
    pos = path->name.len + 1;
    // 文件名前面的斜杠
    file->name.data[path->name.len + path->len]  = '/';

    for (i = 0; i < 3; i++) {
        level = path->level[i];

        if (level == 0) {
            break;
        }
        // level的值为该层子目录的名字长度
        name -= level;
        // 根目录后面的斜杠
        file->name.data[pos - 1] = '/';
        // 把文件名中的一部分复制到该层子目录的名字处
        ngx_memcpy(&file->name.data[pos], &file->name.data[name], level);
        pos += level + 1;// 每层都需要加一个斜杠，所以要加一
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, file->log, 0,
                   "hashed path: %s", file->name.data);
}

// 逐层创建子目录
int ngx_create_path(ngx_file_t *file, ngx_path_t *path)
{
    int        i, pos;
    ngx_err_t  err;

    pos = path->name.len;

    for (i = 0; i < 3; i++) {
        if (path->level[i] == 0) {
            break;
        }

        pos += path->level[i] + 1;

        file->name.data[pos] = '\0';

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, file->log, 0,
                       "temp file: \"%s\"", file->name.data);

        if (ngx_create_dir(file->name.data) == NGX_FILE_ERROR) {
            err = ngx_errno;
            if (err != NGX_EEXIST) {
                ngx_log_error(NGX_LOG_CRIT, file->log, err,
                              ngx_create_dir_n " \"%s\" failed",
                              file->name.data);
                return NGX_ERROR;
            }
        }

        file->name.data[pos] = '/';
    }

    return NGX_OK;
}


void ngx_init_temp_number()
{
    ngx_random = 0;

    ngx_temp_number = ngx_random;

    while (ngx_random < 10000) {
        ngx_random = 123456;
    }
}


ngx_uint_t ngx_next_temp_number(ngx_uint_t collision)
{
    if (collision) {
        ngx_temp_number += ngx_random;
    }

    return ngx_temp_number++;
}


char *ngx_conf_set_path_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_int_t    level;
    ngx_uint_t   i, n;
    ngx_str_t   *value;
    ngx_path_t  *path, **pp;

    pp = (ngx_path_t **) (p + cmd->offset);

    if (*pp) {
        return "is duplicate";
    }

    /* TODO: check duplicate in cf->cycle->pathes */

    ngx_test_null(path, ngx_pcalloc(cf->pool, sizeof(ngx_path_t)),
                  NGX_CONF_ERROR);

    *pp = path;

    ngx_test_null(pp, ngx_push_array(&cf->cycle->pathes), NGX_CONF_ERROR);
    *pp = path;

    value = (ngx_str_t *) cf->args->elts;
    // 根目录，指令形如proxy_temp_path /usr/local/nginx
    path->name = value[1];
    // 初始化子目录长度为0
    path->len = 0;
    // 设置每层子目录长度
    for (i = 0, n = 2; n < cf->args->nelts; i++, n++) {
        level = ngx_atoi(value[n].data, value[n].len);
        if (level == NGX_ERROR || level == 0) {
            return "invalid value";
        }

        path->level[i] = level;
        path->len += level + 1;// 目录后加\，占一字节
    }
    // 填充0
    while (i < 3) {
        path->level[i++] = 0;
    }

    path->gc_handler = (ngx_gc_handler_pt) cmd->post;

    return NGX_CONF_OK;
}
