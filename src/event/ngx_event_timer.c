
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (NGX_THREADS)
ngx_mutex_t  *ngx_event_timer_mutex;
#endif


ngx_thread_volatile ngx_rbtree_t  *ngx_event_timer_rbtree;
ngx_rbtree_t                       ngx_event_timer_sentinel;

// 初始化时间红黑树
ngx_int_t ngx_event_timer_init(ngx_log_t *log)
{
    if (ngx_event_timer_rbtree) {
#if (NGX_THREADS)
        ngx_event_timer_mutex->log = log;
#endif
        return NGX_OK;
    }
    // 指向哨兵节点
    ngx_event_timer_rbtree = &ngx_event_timer_sentinel;

#if (NGX_THREADS)
    if (!(ngx_event_timer_mutex = ngx_mutex_init(log, 0))) {
        return NGX_ERROR;
    }
#endif

    return NGX_OK;
}

// 找出最快到期的节点的值  
ngx_msec_t ngx_event_find_timer(void)
{
    ngx_msec_t     timer;
    ngx_rbtree_t  *node;
    // 红黑树中没有节点
    if (ngx_event_timer_rbtree == &ngx_event_timer_sentinel) {
        return NGX_TIMER_INFINITE;
    }

    if (ngx_mutex_lock(ngx_event_timer_mutex) == NGX_ERROR) {
        return NGX_TIMER_ERROR;
    }
    //
    node = ngx_rbtree_min((ngx_rbtree_t *) ngx_event_timer_rbtree,
                          &ngx_event_timer_sentinel);

    ngx_mutex_unlock(ngx_event_timer_mutex);

    timer = (ngx_msec_t)
         (node->key * NGX_TIMER_RESOLUTION -
               ngx_elapsed_msec / NGX_TIMER_RESOLUTION * NGX_TIMER_RESOLUTION);
#if 0
                         (node->key * NGX_TIMER_RESOLUTION - ngx_elapsed_msec);
#endif

    return timer > 0 ? timer: 0 ;
}

void ngx_event_expire_timers(ngx_msec_t timer)
{   
    ngx_event_t   *ev;
    ngx_rbtree_t  *node;

    if (timer < 0) {
        /* avoid the endless loop if the time goes backward for some reason */
        timer = 0;
    }

    for ( ;; ) {

        if (ngx_event_timer_rbtree == &ngx_event_timer_sentinel) {
            return;
        }

        if (ngx_mutex_lock(ngx_event_timer_mutex) == NGX_ERROR) {
            return;
        }

        node = ngx_rbtree_min((ngx_rbtree_t *) ngx_event_timer_rbtree,
                              &ngx_event_timer_sentinel);

        if (node->key <= (ngx_msec_t)
                         (ngx_old_elapsed_msec + timer) / NGX_TIMER_RESOLUTION)
        {   // 指向event结构体首地址,rbtree_key是event结构体里的一个字段
            ev = (ngx_event_t *)
                           ((char *) node - offsetof(ngx_event_t, rbtree_key));

#if (NGX_THREADS)

            if (ngx_threaded && ngx_trylock(ev->lock) == 0) {

                /*
                 * We can not change the timer of the event that is been
                 * handling by another thread.  And we can not easy walk
                 * the rbtree to find a next expired timer so we exit the loop.
                 * However it should be rare case when the event that is
                 * been handling has expired timer.
                 */

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "event " PTR_FMT " is busy in expire timers",
                               ev);
                break;
            }
#endif

            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                           "event timer del: %d: %d",
                            ngx_event_ident(ev->data), ev->rbtree_key);
            // 节点已经超时，从红黑树中删除，然后执行回调
            ngx_rbtree_delete((ngx_rbtree_t **) &ngx_event_timer_rbtree,
                              &ngx_event_timer_sentinel,
                              (ngx_rbtree_t *) &ev->rbtree_key);

            ngx_mutex_unlock(ngx_event_timer_mutex);

#if (NGX_DEBUG)
            ev->rbtree_left = NULL;
            ev->rbtree_right = NULL;
            ev->rbtree_parent = NULL;
#endif

            ev->timer_set = 0;

#if (NGX_THREADS)
            if (ngx_threaded) {
                if (ngx_mutex_lock(ngx_posted_events_mutex) == NGX_ERROR) {
                    return;
                }

                ev->posted_timedout = 1;
                ngx_post_event(ev);

                ngx_mutex_unlock(ngx_posted_events_mutex);

                ngx_unlock(ev->lock);

                continue;
            }
#endif
            // 标记该事件超时
            ev->timedout = 1;
            // 在handler中需要判断是否超时，这里不能不执行handler，因为有的handler就是在超时时才执行
            ev->event_handler(ev);

            continue;
        }

        break;
    }

    ngx_mutex_unlock(ngx_event_timer_mutex);
}
