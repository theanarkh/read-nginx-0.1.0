
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_EVENT_CONNECT_H_INCLUDED_
#define _NGX_EVENT_CONNECT_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define NGX_CONNECT_ERROR   -10

// 一个对端的信息
typedef struct {
    in_addr_t          addr;
    ngx_str_t          host;
    in_port_t          port;
    ngx_str_t          addr_port_text;

    ngx_int_t          fails;// 失败次数
    time_t             accessed;// 最近一个连接失败时间
} ngx_peer_t;


typedef struct {
    ngx_int_t           current;// 当前连接的对端
    ngx_int_t           number;// 对端数
    ngx_int_t           max_fails;// 所有对端最大连接失败次数
    ngx_int_t           fail_timeout;// 所有对端连接失败后，隔fail_timeout才继续使用该对端
    ngx_int_t           last_cached;

 /* ngx_mutex_t        *mutex; */
    ngx_connection_t  **cached;// 对端缓存的连接

    ngx_peer_t          peers[1];// 一到多个对端
} ngx_peers_t;


typedef struct {
    ngx_peers_t       *peers;// 执行一个ngx_peers_t结构体
    ngx_int_t          cur_peer;// 当前使用的对端
    ngx_int_t          tries;// 等于对端的个数，连接一个对端失败则减一

    ngx_connection_t  *connection;
#if (NGX_THREADS)
    ngx_atomic_t      *lock;
#endif
    
    int                rcvbuf;// 接收缓冲区大小

    ngx_log_t         *log;

    unsigned           cached:1;// 该connection结构体是否来自缓存
    unsigned           log_error:2;  /* ngx_connection_log_error_e */
} ngx_peer_connection_t;


int ngx_event_connect_peer(ngx_peer_connection_t *pc);
void ngx_event_connect_peer_failed(ngx_peer_connection_t *pc);


#endif /* _NGX_EVENT_CONNECT_H_INCLUDED_ */
