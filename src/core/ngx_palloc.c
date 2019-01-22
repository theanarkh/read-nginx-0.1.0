
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;
    // 在堆上分配一块大小为size的连续虚拟内存
    if (!(p = ngx_alloc(size, log))) {
       return NULL;
    }
    // last代表没有使用的空间首地址，分配的内存前面保存ngx_pool_t结构体
    p->last = (char *) p + sizeof(ngx_pool_t);
    // end代表整个池子的最大偏移
    p->end = (char *) p + size;
    // 下一个池子
    p->next = NULL;
    // 用于分配大块内存的池子
    p->large = NULL;
    p->log = log;

    return p;
}


void ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p, *n;
    ngx_pool_large_t  *l;
    // 释放大块内存对应的池子
    for (l = pool->large; l; l = l->next) {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: " PTR_FMT, l->alloc);

        if (l->alloc) {
            free(l->alloc);
        }
    }

#if (NGX_DEBUG)

    /*
     * we could allocate the pool->log from this pool
     * so we can not use this log while the free()ing the pool
     */

    for (p = pool, n = pool->next; /* void */; p = n, n = n->next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: " PTR_FMT ", unused: " SIZE_T_FMT,
                       p, p->end - p->last);

        if (n == NULL) {
            break;
        }
    }

#endif
    // p指向当前需要释放的池子，n指向当前下一个池子，即待释放的池子（如果有的话）
    for (p = pool, n = pool->next; /* void */; p = n, n = n->next) {
        free(p);
        // 没有下一个池子，退出循环
        if (n == NULL) {
            break;
        }
    }
}

// 在池子上分配size大小的内存
void *ngx_palloc(ngx_pool_t *pool, size_t size)
{
    char              *m;
    ngx_pool_t        *p, *n;
    ngx_pool_large_t  *large, *last;
    /*
        如果需要分配的内存没有超过大块内存大小，并且没有超过pool最大可使用的空间，则可能可以在pool上分配
        pool指向池子首地址，pool->end - (char *) pool) - sizeof(ngx_pool_t)代表池子最大分配的内存，
        不一定等于为分配的大小，end-last才是未分配的大小
    */
    if (size <= (size_t) NGX_MAX_ALLOC_FROM_POOL
        && size <= (size_t) (pool->end - (char *) pool) - sizeof(ngx_pool_t))
    {
        for (p = pool, n = pool->next; /* void */; p = n, n = n->next) {
            // 内存对齐，得到可分配地址的首地址
            m = ngx_align(p->last);
            // 如果未使用的空间大于等于size，则直接在pool上分配
            if ((size_t) (p->end - m) >= size) {
                p->last = m + size ;// 算出最新的未使用地址首地址
                // 返回分配的内存首地址
                return m;
            }
            // 如果当前块内存不足，则到下一块找，如果没有下一块，则结束循环
            if (n == NULL) {
                break;
            }
        }

        /* allocate a new pool block */
        // 跑到这里说明当前的池子内存不够，则申请一个新的池子，大小为当前池子规格
        if (!(n = ngx_create_pool((size_t) (p->end - (char *) p), p->log))) {
            return NULL;
        }
        // 插入当前链表
        p->next = n;
        // 直接分配内存并重算last的值，if那里保证了新申请的池子是一定可以大于等于size的
        m = n->last;
        n->last += size;

        return m;
    }

    /* allocate a large block */

    large = NULL;
    last = NULL;
    // 分配大块内存
    if (pool->large) {
        /*
            如果当前大块池子有可用内存，并且下一块不为空，则循环，
            如果当前池子没有可用内存或下一块为空则结束循环。
            即找到最后一个可用的池子，由last保存
        */
        for (last = pool->large; /* void */ ; last = last->next) {
            if (last->alloc == NULL) {
                large = last;
                last = NULL;
                break;
            }

            if (last->next == NULL) {
                break;
            }
        }
    }

    if (large == NULL) {
        if (!(large = ngx_palloc(pool, sizeof(ngx_pool_large_t)))) {
            return NULL;
        }

        large->next = NULL;
    }

#if 0
    if (!(p = ngx_memalign(ngx_pagesize, size, pool->log))) {
        return NULL;
    }
#else
    if (!(p = ngx_alloc(size, pool->log))) {
        return NULL;
    }
#endif

    if (pool->large == NULL) {
        pool->large = large;

    } else if (last) {
        last->next = large;
    }

    large->alloc = p;

    return p;
}


ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: " PTR_FMT, l->alloc);
            free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


void *ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}

#if 0

static void *ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
