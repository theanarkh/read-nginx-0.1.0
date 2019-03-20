
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * the single part format:
 *
 * "HTTP/1.0 206 Partial Content" CRLF
 * ... header ...
 * "Content-Type: image/jpeg" CRLF
 * "Content-Length: SIZE" CRLF
 * "Content-Range: bytes START-END/SIZE" CRLF
 * CRLF
 * ... data ...
 *
 *
 * the mutlipart format:
 *
 * "HTTP/1.0 206 Partial Content" CRLF
 * ... header ...
 * "Content-Type: multipart/byteranges; boundary=0123456789" CRLF
 * CRLF
 * CRLF
 * "--0123456789" CRLF
 * "Content-Type: image/jpeg" CRLF
 * "Content-Range: bytes START0-END0/SIZE" CRLF
 * CRLF
 * ... data ...
 * CRLF
 * "--0123456789" CRLF
 * "Content-Type: image/jpeg" CRLF
 * "Content-Range: bytes START1-END1/SIZE" CRLF
 * CRLF
 * ... data ...
 * CRLF
 * "--0123456789--" CRLF
 */


typedef struct {
    ngx_str_t  boundary_header;
} ngx_http_range_filter_ctx_t;


static ngx_int_t ngx_http_range_header_filter_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_http_range_body_filter_init(ngx_cycle_t *cycle);


static ngx_http_module_t  ngx_http_range_header_filter_module_ctx = {
    NULL,                                  /* pre conf */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_range_header_filter_module = {
    NGX_MODULE,
    &ngx_http_range_header_filter_module_ctx, /* module context */
    NULL,                                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    ngx_http_range_header_filter_init,     /* init module */
    NULL                                   /* init child */
};


static ngx_http_module_t  ngx_http_range_body_filter_module_ctx = {
    NULL,                                  /* pre conf */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_range_body_filter_module = {
    NGX_MODULE,
    &ngx_http_range_body_filter_module_ctx, /* module context */
    NULL,                                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    ngx_http_range_body_filter_init,       /* init module */
    NULL                                   /* init child */
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;


static ngx_int_t ngx_http_range_header_filter(ngx_http_request_t *r)
{
    ngx_int_t                     rc;
    ngx_uint_t                    boundary, suffix, i;
    u_char                       *p;
    size_t                        len;
    off_t                         start, end;
    ngx_http_range_t             *range;
    ngx_http_range_filter_ctx_t  *ctx;

    if (r->http_version < NGX_HTTP_VERSION_10
        || r->headers_out.status != NGX_HTTP_OK
        || r->headers_out.content_length_n == -1
        || !r->filter_allow_ranges)
    {
        return ngx_http_next_header_filter(r);
    }

    if (r->headers_in.range == NULL
        || r->headers_in.range->value.len < 7
        || ngx_strncasecmp(r->headers_in.range->value.data, "bytes=", 6) != 0)
    {

        r->headers_out.accept_ranges = ngx_list_push(&r->headers_out.headers);
        if (r->headers_out.accept_ranges == NULL) {
            return NGX_ERROR;
        }

        r->headers_out.accept_ranges->key.len = sizeof("Accept-Ranges") - 1;
        r->headers_out.accept_ranges->key.data = (u_char *) "Accept-Ranges";
        r->headers_out.accept_ranges->value.len = sizeof("bytes") - 1;
        r->headers_out.accept_ranges->value.data = (u_char *) "bytes";

        return ngx_http_next_header_filter(r);
    }
    // 申请5个range
    ngx_init_array(r->headers_out.ranges, r->pool, 5, sizeof(ngx_http_range_t),
                   NGX_ERROR);

    rc = 0;
    range = NULL;
    // 拿到客户端请求的range范围
    p = r->headers_in.range->value.data + 6;
    // 每一个循环解析一个range
    for ( ;; ) {
        start = 0;
        end = 0;
        suffix = 0;
        // 容错，跳过空格
        while (*p == ' ') { p++; }
        /*
            格式：
                a-b
                -b
                a-
                a-b,c-
                -a
                不写开头则说明是从倒数abs(-a)字节开始，不写结尾则说明是到最后一个字节，
                内容可能是多个以上的组合
        */
        // 第一个有效字符不是-，则说明是属于a-[b]格式,b可能没有
        if (*p != '-') {
            // 不是数字则报错
            if (*p < '0' || *p > '9') {
                rc = NGX_HTTP_RANGE_NOT_SATISFIABLE;
                break;
            }
            // 整数化a，直到遇到第一个非数字
            while (*p >= '0' && *p <= '9') {
                start = start * 10 + *p++ - '0';
            }
            // 容错，过滤空格
            while (*p == ' ') { p++; }
            // 第一个数字后面不是-则报错
            if (*p++ != '-') {
                rc = NGX_HTTP_RANGE_NOT_SATISFIABLE;
                break;
            }
            // 请求的范围大于返回内容的大小范围，则报错
            if (start >= r->headers_out.content_length_n) {
                rc = NGX_HTTP_RANGE_NOT_SATISFIABLE;
                break;
            }
            // 容错，过滤-后面的空格
            while (*p == ' ') { p++; }
            // 如果a-后面是逗号或者\0则说明是最后一个range
            if (*p == ',' || *p == '\0') {
                ngx_test_null(range, ngx_push_array(&r->headers_out.ranges),
                              NGX_ERROR);
                // 记录该range的范围，从start到最后一个字节
                range->start = start;
                range->end = r->headers_out.content_length_n;
                // 如果当前字符是\0则直接break退出循环，解析结束
                if (*p++ != ',') {
                    break;
                }
                // 否则继续解析下一个range
                continue;
            }

        } else {
            // range的第一个字节是-说明是-a格式，不是代表从第一个字节开始，而是到倒数第几个算起
            suffix = 1;
            p++;
        }
        // 走到这说明是需要解析end的，即不是a-(,|\0)这种格式,但是可能是a-b或者-b格式
        if (*p < '0' || *p > '9') {
            rc = NGX_HTTP_RANGE_NOT_SATISFIABLE;
            break;
        }
        // 不管是哪种格式，首先计算end
        while (*p >= '0' && *p <= '9') {
            end = end * 10 + *p++ - '0';
        }

        while (*p == ' ') { p++; }
        // end的后面不是或\0则格式错误
        if (*p != ',' && *p != '\0') {
            rc = NGX_HTTP_RANGE_NOT_SATISFIABLE;
            break;
        }
        /*
            计算完end再计算start，没有写range的开始字节，
            则说明start是倒数第几个字节，end这时候代表是一直到最后，而不是结束字节的位置
        */
        if (suffix) {
           start = r->headers_out.content_length_n - end;
           // 为了统一处理-b和a-b两种格式，下面统一进行了end+1处理，所以这里需要先减一
           end = r->headers_out.content_length_n - 1;
        }
        // 范围不合法
        if (start > end) {
            rc = NGX_HTTP_RANGE_NOT_SATISFIABLE;
            break;
        }
        // 记录到一个range结构体中
        ngx_test_null(range, ngx_push_array(&r->headers_out.ranges), NGX_ERROR);
        range->start = start;
        // 取最小值
        if (end >= r->headers_out.content_length_n) {
            /*
             * Download Accelerator sends the last byte position
             * that equals to the file length
             */
            range->end = r->headers_out.content_length_n;

        } else {
            range->end = end + 1;
        }
        // 不等于逗号说明等于\0，解析结束        
        if (*p++ != ',') {
            break;
        }
    }
    // 解析出错
    if (rc) {

        /* rc == NGX_HTTP_RANGE_NOT_SATISFIABLE */
        // 状态是416，说明请求的范围不合法，返回合法的范围给客户端
        r->headers_out.status = rc;
        r->headers_out.ranges.nelts = 0;

        r->headers_out.content_range = ngx_list_push(&r->headers_out.headers);
        if (r->headers_out.content_range == NULL) {
            return NGX_ERROR;
        }

        r->headers_out.content_range->key.len = sizeof("Content-Range") - 1;
        r->headers_out.content_range->key.data = (u_char *) "Content-Range";

        r->headers_out.content_range->value.data =
                                               ngx_palloc(r->pool, 8 + 20 + 1);
        if (r->headers_out.content_range->value.data == NULL) {
            return NGX_ERROR;
        }

        r->headers_out.content_range->value.len =
                ngx_snprintf((char *) r->headers_out.content_range->value.data,
                             8 + 20 + 1, "bytes */" OFF_T_FMT,
                             r->headers_out.content_length_n);
        // 不返回数据，清空content-length
        r->headers_out.content_length_n = -1;
        if (r->headers_out.content_length) {
            r->headers_out.content_length->key.len = 0;
            r->headers_out.content_length = NULL;
        }

        return rc;

    } else {
        // 206
        r->headers_out.status = NGX_HTTP_PARTIAL_CONTENT;
        // 只有一个range，设置range相关的响应头
        if (r->headers_out.ranges.nelts == 1) {

            r->headers_out.content_range =
                                        ngx_list_push(&r->headers_out.headers);
            if (r->headers_out.content_range == NULL) {
                return NGX_ERROR;
            }

            r->headers_out.content_range->key.len = sizeof("Content-Range") - 1;
            r->headers_out.content_range->key.data = (u_char *) "Content-Range";

            ngx_test_null(r->headers_out.content_range->value.data,
                          ngx_palloc(r->pool, 6 + 20 + 1 + 20 + 1 + 20 + 1),
                          NGX_ERROR);

            /* "Content-Range: bytes SSSS-EEEE/TTTT" header */

            r->headers_out.content_range->value.len =
                   ngx_snprintf((char *)
                                r->headers_out.content_range->value.data,
                                6 + 20 + 1 + 20 + 1 + 20 + 1,
                                "bytes " OFF_T_FMT "-" OFF_T_FMT "/" OFF_T_FMT,
                                range->start, range->end - 1,
                                r->headers_out.content_length_n);

            r->headers_out.content_length_n = range->end - range->start;

        } else {

#if 0
            /* TODO: what if no content_type ?? */

            if (!(r->headers_out.content_type =
                   ngx_http_add_header(&r->headers_out, ngx_http_headers_out)))
            {
                return NGX_ERROR;
            }
#endif
            // 多个range返回的格式不一样
            ngx_http_create_ctx(r, ctx, ngx_http_range_body_filter_module,
                                sizeof(ngx_http_range_filter_ctx_t), NGX_ERROR);

            len = 4 + 10 + 2 + 14 + r->headers_out.content_type->value.len
                                  + 2 + 21 + 1;

            if (r->headers_out.charset.len) {
                len += 10 + r->headers_out.charset.len;
            }

            ngx_test_null(ctx->boundary_header.data, ngx_palloc(r->pool, len),
                          NGX_ERROR);
            // 获取一个分隔多个range内容的字符串
            boundary = ngx_next_temp_number(0);

            /*
             * The boundary header of the range:
             * CRLF
             * "--0123456789" CRLF
             * "Content-Type: image/jpeg" CRLF
             * "Content-Range: bytes "
             */
            // 设置响应头
            if (r->headers_out.charset.len) {
                ctx->boundary_header.len =
                         ngx_snprintf((char *) ctx->boundary_header.data, len,
                                      CRLF "--%010" NGX_UINT_T_FMT CRLF
                                      "Content-Type: %s; charset=%s" CRLF
                                      "Content-Range: bytes ",
                                      boundary,
                                      r->headers_out.content_type->value.data,
                                      r->headers_out.charset.data);

                r->headers_out.charset.len = 0;

            } else {
                ctx->boundary_header.len =
                         ngx_snprintf((char *) ctx->boundary_header.data, len,
                                      CRLF "--%010" NGX_UINT_T_FMT CRLF
                                      "Content-Type: %s" CRLF
                                      "Content-Range: bytes ",
                                      boundary,
                                      r->headers_out.content_type->value.data);
            }
            // 更新content-type为multipart/byteranges; boundary="xxx"
            ngx_test_null(r->headers_out.content_type->value.data,
                          ngx_palloc(r->pool, 31 + 10 + 1),
                          NGX_ERROR);

            /* "Content-Type: multipart/byteranges; boundary=0123456789" */

            r->headers_out.content_type->value.len =
                      ngx_snprintf((char *)
                                   r->headers_out.content_type->value.data,
                                   31 + 10 + 1,
                                   "multipart/byteranges; boundary=%010"
                                   NGX_UINT_T_FMT,
                                   boundary);

            /* the size of the last boundary CRLF "--0123456789--" CRLF */
            len = 4 + 10 + 4;
            /*  
                设置响应头
                xxxx
                Content-Type: image/png
                Content-Range: bytes a-b/content-length
                内容
                xxxx
                Content-Type: image/png
                Content-Range: bytes c-d/content-length
                内容
                xxxx
                // 这里貌似没有设置每个range里的content-type
            */
            range = r->headers_out.ranges.elts;
            for (i = 0; i < r->headers_out.ranges.nelts; i++) {
                // content_range记录了一个range的信息， Content-Range: bytes a-b/content-length
                ngx_test_null(range[i].content_range.data,
                              ngx_palloc(r->pool, 20 + 1 + 20 + 1 + 20 + 5),
                              NGX_ERROR);

                /* the size of the range: "SSSS-EEEE/TTTT" CRLF CRLF */
                // 这里的content_length_n是有效数据的长度
                range[i].content_range.len =
                  ngx_snprintf((char *) range[i].content_range.data,
                               20 + 1 + 20 + 1 + 20 + 5,
                               OFF_T_FMT "-" OFF_T_FMT "/" OFF_T_FMT CRLF CRLF,
                               range[i].start, range[i].end - 1,
                               r->headers_out.content_length_n);
                // 这里len是最后返回的数据的长度
                len += ctx->boundary_header.len + range[i].content_range.len
                                    + (size_t) (range[i].end - range[i].start);
            }

            r->headers_out.content_length_n = len;
            r->headers_out.content_length = NULL;
        }
    }

    return ngx_http_next_header_filter(r);
}


static ngx_int_t ngx_http_range_body_filter(ngx_http_request_t *r,
                                            ngx_chain_t *in)
{
    ngx_uint_t                    i;
    ngx_buf_t                    *b;
    ngx_chain_t                  *out, *hcl, *rcl, *dcl, **ll;
    ngx_http_range_t             *range;
    ngx_http_range_filter_ctx_t  *ctx;
    // 没有则跳过，在header filter里设置
    if (r->headers_out.ranges.nelts == 0) {
        return ngx_http_next_body_filter(r, in);
    }

    /*
     * the optimized version for the static files only
     * that are passed in the single file buf
     */

    if (in && in->buf->in_file && in->buf->last_buf) {
        range = r->headers_out.ranges.elts;

        if (r->headers_out.ranges.nelts == 1) {
            in->buf->file_pos = range->start;
            in->buf->file_last = range->end;

            return ngx_http_next_body_filter(r, in);
        }
        // 获取boundary内容
        ctx = ngx_http_get_module_ctx(r, ngx_http_range_body_filter_module);
        ll = &out;

        for (i = 0; i < r->headers_out.ranges.nelts; i++) {

            /*
             * The boundary header of the range:
             * CRLF
             * "--0123456789" CRLF
             * "Content-Type: image/jpeg" CRLF
             * "Content-Range: bytes "
             */

            ngx_test_null(b, ngx_calloc_buf(r->pool), NGX_ERROR);
            b->memory = 1;
            b->pos = ctx->boundary_header.data;
            b->last = ctx->boundary_header.data + ctx->boundary_header.len;

            ngx_test_null(hcl, ngx_alloc_chain_link(r->pool), NGX_ERROR);
            hcl->buf = b;

            /* "SSSS-EEEE/TTTT" CRLF CRLF */

            ngx_test_null(b, ngx_calloc_buf(r->pool), NGX_ERROR);
            b->temporary = 1;
            b->pos = range[i].content_range.data;
            b->last = range[i].content_range.data + range[i].content_range.len;

            ngx_test_null(rcl, ngx_alloc_chain_link(r->pool), NGX_ERROR);
            rcl->buf = b;

            /* the range data */

            ngx_test_null(b, ngx_calloc_buf(r->pool), NGX_ERROR);
            b->in_file = 1;
            b->file_pos = range[i].start;
            b->file_last = range[i].end;
            b->file = in->buf->file;

            ngx_alloc_link_and_set_buf(dcl, b, r->pool, NGX_ERROR);

            *ll = hcl;
            hcl->next = rcl;
            rcl->next = dcl;
            ll = &dcl->next;
        }

        /* the last boundary CRLF "--0123456789--" CRLF  */

        ngx_test_null(b, ngx_calloc_buf(r->pool), NGX_ERROR);
        b->temporary = 1;
        b->last_buf = 1;
        ngx_test_null(b->pos, ngx_palloc(r->pool, 4 + 10 + 4), NGX_ERROR);
        b->last = ngx_cpymem(b->pos, ctx->boundary_header.data, 4 + 10);
        *b->last++ = '-'; *b->last++ = '-';
        *b->last++ = CR; *b->last++ = LF;

        ngx_alloc_link_and_set_buf(hcl, b, r->pool, NGX_ERROR);
        *ll = hcl;

        return ngx_http_next_body_filter(r, out);
    }

    /* TODO: alert */

    return ngx_http_next_body_filter(r, in);
}

// 挂载到header和body的filter链表上
static ngx_int_t ngx_http_range_header_filter_init(ngx_cycle_t *cycle)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_range_header_filter;

    return NGX_OK;
}


static ngx_int_t ngx_http_range_body_filter_init(ngx_cycle_t *cycle)
{
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_range_body_filter;

    return NGX_OK;
}
