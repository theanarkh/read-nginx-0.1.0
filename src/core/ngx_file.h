
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_FILE_H_INCLUDED_
#define _NGX_FILE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

typedef struct ngx_path_s  ngx_path_t;

#include <ngx_garbage_collector.h>


struct ngx_file_s {
    ngx_fd_t         fd;// 文件描述符
    ngx_str_t        name;// 文件名
    ngx_file_info_t  info;// 文件的属性描述

    off_t            offset;// 文件偏移
    off_t            sys_offset;

    ngx_log_t       *log;

    unsigned         info_valid:1;
};

#define NGX_MAX_PATH_LEVEL  3

struct ngx_path_s {
    ngx_str_t           name;// 根目录名字
    u_int               len;// level层子目录的总长度
    u_int               level[3];// 每层随机子目录长度，最多三层
    ngx_gc_handler_pt   gc_handler;
};


typedef struct {
    ngx_file_t   file;
    off_t        offset;
    ngx_path_t  *path;
    ngx_pool_t  *pool;
    char        *warn;

    unsigned     persistent:1;
} ngx_temp_file_t;


int ngx_write_chain_to_temp_file(ngx_temp_file_t *tf, ngx_chain_t *chain);
int ngx_create_temp_file(ngx_file_t *file, ngx_path_t *path,
                         ngx_pool_t *pool, int persistent);
void ngx_create_hashed_filename(ngx_file_t *file, ngx_path_t *path);
int ngx_create_path(ngx_file_t *file, ngx_path_t *path);

void ngx_init_temp_number();
ngx_uint_t ngx_next_temp_number(ngx_uint_t collision);

char *ngx_conf_set_path_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


#define ngx_conf_merge_path_value(conf, prev, path, l1, l2, l3, pool)        \
    if (conf == NULL) {                                                      \
        if (prev == NULL) {                                                  \
            ngx_test_null(conf, ngx_palloc(pool, sizeof(ngx_path_t)), NULL); \
            conf->name.len = sizeof(path) - 1;                               \
            conf->name.data = (u_char *) path;                               \
            conf->level[0] = l1;                                             \
            conf->level[1] = l2;                                             \
            conf->level[2] = l3;                                             \
            conf->len = l1 + l2 + l3 + (l1 ? 1:0) + (l2 ? 1:0) + (l3 ? 1:0); \
        } else {                                                             \
            conf = prev;                                                     \
        }                                                                    \
    }



#endif /* _NGX_FILE_H_INCLUDED_ */
