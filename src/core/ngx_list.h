
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_LIST_H_INCLUDED_
#define _NGX_LIST_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_list_part_s  ngx_list_part_t;

struct ngx_list_part_s {
    void             *elts; // 管理的内存首地址
    ngx_uint_t        nelts; // 已使用个数
    ngx_list_part_t  *next;
};


typedef struct {
    ngx_list_part_t  *last; // 指向当前还有空闲内存的ngx_list_part_s结构体
    ngx_list_part_t   part; // ngx_list_part_s链表的第一个节点，相当于头指针 
    /*  
        size: 每个ngx_list_part_s结构体管理的内存中，分配的粒度，即每个元素的大小
        nalloc: 每个ngx_list_part_s结构体可以分配几个size大小的内存，
        nalloc和size是ngx_list_part_s结构体的共同属性，所以放到全局
    */
    size_t            size; 
    ngx_uint_t        nalloc; 
    ngx_pool_t       *pool;
} ngx_list_t;


ngx_inline static ngx_int_t ngx_list_init(ngx_list_t *list, ngx_pool_t *pool,
                                          ngx_uint_t n, size_t size)
{   
    // 分配第一个ngx_list_part_t节点
    if (!(list->part.elts = ngx_palloc(pool, n * size))) {
        return NGX_ERROR;
    }
    // 初始化使用个数为0,
    list->part.nelts = 0;
    list->part.next = NULL;
    // 指向当前可分配内存的节点
    list->last = &list->part;
    // 粒度
    list->size = size;
    // 每个ngx_list_part_t结构体可分配的块个数
    list->nalloc = n;
    list->pool = pool;

    return NGX_OK;
}


/*
 *
 *  the iteration through the list:
 *
 *  part = &list.part;
 *  data = part->elts;
 *
 *  for (i = 0 ;; i++) {
 *
 *      if (i >= part->nelts) {
 *          if (part->next == NULL) {
 *              break;
 *          }
 *
 *          part = part->next;
 *          data = part->elts;
 *          i = 0;
 *      }
 *
 *      ...  data[i] ...
 *
 *  }
 */


void *ngx_list_push(ngx_list_t *list);


#endif /* _NGX_LIST_H_INCLUDED_ */
