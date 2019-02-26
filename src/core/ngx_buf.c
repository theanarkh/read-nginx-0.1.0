
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_buf_t *ngx_create_temp_buf(ngx_pool_t *pool, size_t size)
{
    ngx_buf_t *b;
    // 从pool中分配一个ngx_buf_t结构体
    if (!(b = ngx_calloc_buf(pool))) {
        return NULL;
    }
    // 从pool中分配大小为size的内存，由ngx_buf_t结构体管理，start指向首地址
    if (!(b->start = ngx_palloc(pool, size))) {
        return NULL;
    }
    
    b->pos = b->start;
    b->last = b->start;
    b->end = b->last + size;
    // 标记为临时buf
    b->temporary = 1;

    /*

    b->file_pos = 0;
    b->file_last = 0;

    b->file = NULL;
    b->shadow = NULL;

    b->tag = 0;

     */

    return b;
}


ngx_chain_t *ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    u_char       *p;
    ngx_int_t     i;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, **ll;
    // 分配num给buf，每个buf管理size大小的内存
    if (!(p = ngx_palloc(pool, bufs->num * bufs->size))) {
        return NULL;
    }

    ll = &chain;

    for (i = 0; i < bufs->num; i++) {
        // 从pool上分配一个buf结构体
        if (!(b = ngx_calloc_buf(pool))) {
            return NULL;
        }
        // 指针都指向当前的p，p代表当前buf结构体管理的内存首地址
        b->pos = p;
        b->last = p;
        b->temporary = 1;

        b->start = p;
        // p递增一个size大小，指向下一个buf结构体管理的内存首地址
        p += bufs->size;
        // end指向管理的内存末尾
        b->end = p;

        /*
        b->file_pos = 0;
        b->file_last = 0;

        b->file = NULL;
        b->shadow = NULL;
        b->tag = 0;
        */
        // 分配一个ngx_chain_s结构体
        if (!(cl = ngx_alloc_chain_link(pool))) {
            return NULL;
        }
        // 链的节点指向当前buf
        cl->buf = b;
        // 修改一个ngx_chain_s *变量的内容为一个ngx_chain_s结构体的地址，即使得ngx_chain_s *变量指向ngx_chain_s结构体
        *ll = cl;
        // 指向当前ngx_chain_s节点next成员的地址，下次把next的值改成新的ngx_chain_s结构体首地址。形成一条链
        ll = &cl->next;
    }
    
    *ll = NULL;
    /*
        形成一条这样的链
        chain变量 => [ngx_chain_s节点] ->  [*buf, *next] -> [*buf, *next] -> NULL
                            ...              |                |       管理size大小的内存
                                           ngx_buf_s        ngx_buf_s <===> [start.....end]      
    */
    return chain;
}

// 把in链表的内容复制到chain链后面，共享buf空间不共享chain节点
ngx_int_t ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain,
                             ngx_chain_t *in)
{
    ngx_chain_t  *cl, **ll;

    ll = chain;
    // *chain代表chain链的首节点地址，chain指向首地址指针变量的地址
    // 找出最后一个节点，ll指向最后一个节点的next域的地址
    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next;
    }

    while (in) {
        // cl指向新分配的chain结构体
        ngx_test_null(cl, ngx_alloc_chain_link(pool), NGX_ERROR);
        // 把当前待复制节点的buf赋给新的chain节点
        cl->buf = in->buf;
        // 把新chain节点追加到chain链最后
        *ll = cl;
        // ll指向当前的最后一个节点，即刚才新分片的节点
        ll = &cl->next;
        // in指针执行下一个节点，待复制
        in = in->next;
    }
    // 新形成的链表最后节点置NULL
    *ll = NULL;

    return NGX_OK;
}


void ngx_chain_update_chains(ngx_chain_t **free, ngx_chain_t **busy,
                             ngx_chain_t **out, ngx_buf_tag_t tag)
{
    ngx_chain_t  *tl;

    if (*busy == NULL) {
        *busy = *out;

    } else {
        for (tl = *busy; /* void */ ; tl = tl->next) {
            if (tl->next == NULL) {
                tl->next = *out;
                break;
            }
        }
    }

    *out = NULL;

    while (*busy) {
        if (ngx_buf_size((*busy)->buf) != 0) {
            break;
        }

#if (HAVE_WRITE_ZEROCOPY)
        if ((*busy)->buf->zerocopy_busy) {
            break;
        }
#endif

        if ((*busy)->buf->tag != tag) {
            *busy = (*busy)->next;
            continue;
        }

        (*busy)->buf->pos = (*busy)->buf->last = (*busy)->buf->start;

        tl = *busy;
        *busy = (*busy)->next;
        tl->next = *free;
        *free = tl;
    }
}
