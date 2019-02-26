
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


void *ngx_list_push(ngx_list_t *l)
{
    void             *elt;
    ngx_list_part_t  *last;
    // 指向当前可用的节点
    last = l->last;
    // 已使用的个数等于最多能使用的个数，说明已经没有可使用的空间，再分配一个ngx_list_part_t
    if (last->nelts == l->nalloc) {

        /* the last part is full, allocate a new list part */
        // 分配一个新的节点
        if (!(last = ngx_palloc(l->pool, sizeof(ngx_list_part_t)))) {
            return NULL;
        }
        // 给上面新分配的节点申请固定大小的内存，供他管理
        if (!(last->elts = ngx_palloc(l->pool, l->nalloc * l->size))) {
            return NULL;
        }
        // 当前新分配的节点已使用块数为0
        last->nelts = 0;
        last->next = NULL;
        // 链成链表，第一个ngx_list_part_t节点充当头指针，可以通过ngx_list_t->part访问
        l->last->next = last;
        // 指向当前可使用的ngx_list_part_t节点
        l->last = last;
    }
    // 当前可分配内存的块首地址加上已经使用的内存，即下一个可用块的首地址
    elt = (char *) last->elts + l->size * last->nelts;
    // 当前节点已分配内存块数加1
    last->nelts++;
    // 返回可使用的内存首地址，在该函数外写入数据
    return elt;
}
