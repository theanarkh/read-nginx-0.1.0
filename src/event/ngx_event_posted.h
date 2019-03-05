
/*
 * Copyright (C) Igomr Sysoev
 */


#ifndef _NGX_EVENT_POSTED_H_INCLUDED_
#define _NGX_EVENT_POSTED_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

// 添加一个节点到队列
#define ngx_post_event(ev)                                                    \
            if (ev->prev == NULL) {                                           \
                // 指向当前的第一个节点
                ev->next = (ngx_event_t *) ngx_posted_events;                 \
                // 指向头指针的地址 
                ev->prev = (ngx_event_t **) &ngx_posted_events;               \
                // 头指针指向新的第一个节点
                ngx_posted_events = ev;                                       \
                // 旧的第一个节点prev指针指向新节点的next字段的地址
                if (ev->next) {                                               \
                    ev->next->prev = &ev->next;                               \
                }                                                             \
                ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,                \
                               "post event " PTR_FMT, ev);                    \
            } else  {                                                         \
                ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,                \
                               "update posted event " PTR_FMT, ev);           \
            }
// 把节点从队列中删除
#define ngx_delete_posted_event(ev)                                           \
        *(ev->prev) = ev->next;                                               \
        if (ev->next) {                                                       \
            ev->next->prev = ev->prev;                                        \
        }                                                                     \
        ev->prev = NULL;                                                      \
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,                        \
                       "delete posted event " PTR_FMT, ev);



void ngx_event_process_posted(ngx_cycle_t *cycle);
void ngx_wakeup_worker_thread(ngx_cycle_t *cycle);

extern ngx_thread_volatile ngx_event_t  *ngx_posted_events;


#if (NGX_THREADS)
ngx_int_t ngx_event_thread_process_posted(ngx_cycle_t *cycle);

extern ngx_mutex_t                      *ngx_posted_events_mutex;
#endif


#endif /* _NGX_EVENT_POSTED_H_INCLUDED_ */
