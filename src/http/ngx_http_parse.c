
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


ngx_int_t ngx_http_parse_request_line(ngx_http_request_t *r, ngx_buf_t *b)
{
    u_char  ch, *p, *m;
    enum {
        sw_start = 0,
        sw_method,
        sw_space_after_method,
        sw_spaces_before_uri,
        sw_schema,
        sw_schema_slash,
        sw_schema_slash_slash,
        sw_host,
        sw_port,
        sw_after_slash_in_uri,
        sw_check_uri,
        sw_uri,
        sw_http_09,
        sw_http_H,
        sw_http_HT,
        sw_http_HTT,
        sw_http_HTTP,
        sw_first_major_digit,
        sw_major_digit,
        sw_first_minor_digit,
        sw_minor_digit,
        sw_almost_done,
        sw_done
    } state;
    // 当前解析状态，因为会多次进入该函数,初始化为0，ngx_memzero(r, sizeof(ngx_http_request_t))
    state = r->state;
    // 开始位置
    p = b->pos;
    // 没解析完并且还有数据
    while (p < b->last && state < sw_done) {
        // 当前字符
        ch = *p++;

        /* gcc 2.95.2 and msvc 6.0 compile this switch as an jump table */

        switch (state) {

        /* HTTP methods: GET, HEAD, POST */
        case sw_start:
            // 因为上面p++了，所以这里减一即当前字符的位置
            r->request_start = p - 1;
            // 忽略开头的回车换行
            if (ch == CR || ch == LF) {
                break;
            }
            // 不是A-Z范围的字符不合法，返回错误 
            if (ch < 'A' || ch > 'Z') {
                return NGX_HTTP_PARSE_INVALID_METHOD;
            }
            // 遇到第一个合法字符，进入解析method状态
            state = sw_method;
            break;

        case sw_method:
            // 遇到空格，http method解析结束
            if (ch == ' ') {
                // 记录method的字符串范围
                r->method_end = p - 1;
                m = r->request_start;
                // method的字符串长度是3，则比较是不是等于GET
                if (r->method_end - m == 3) {

                    if (m[0] == 'G' && m[1] == 'E' && m[2] == 'T') {
                        r->method = NGX_HTTP_GET;
                    }

                } else if (r->method_end - m == 4) {

                    if (m[0] == 'P' && m[1] == 'O'
                        && m[2] == 'T' && m[3] == 'T')
                    {
                        r->method = NGX_HTTP_POST;

                    } else if (m[0] == 'H' && m[1] == 'E'
                               && m[2] == 'A' && m[3] == 'D')
                    {
                        r->method = NGX_HTTP_HEAD;
                    }
                }
                // 进入解析url前的空格状态
                state = sw_spaces_before_uri;
                break;
            }

            if (ch < 'A' || ch > 'Z') {
                return NGX_HTTP_PARSE_INVALID_METHOD;
            }

            break;

        /* single space after method */
        case sw_space_after_method:
            switch (ch) {
            case ' ':
                state = sw_spaces_before_uri;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_METHOD;
            }
            break;

        /* space* before URI */
        case sw_spaces_before_uri:
            switch (ch) {
            // 遇到斜杠，即url的第一个字符，说明是相对地址
            case '/':
                r->uri_start = p - 1;
                state = sw_after_slash_in_uri;
                break;
            // 忽略url前的空格
            case ' ':
                break;
            default:
                // 遇到的第一个字符不是斜杠说明是绝对地址，进入解析协议状态
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                    // 记录协议字符的起始位置
                    r->schema_start = p - 1;
                    state = sw_schema;
                    break;
                }
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;
        // 解析协议
        case sw_schema:
            switch (ch) {
            // 遇到冒号说明协议解析完毕，进入解析协议后的斜杠状态
            case ':':
                r->schema_end = p - 1;
                state = sw_schema_slash;
                break;
            default:
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                    break;
                }
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_schema_slash:
            switch (ch) {
            // 遇到协议后的第一个斜杠，进入遇到遇到了两个斜杠的状态
            case '/':
                state = sw_schema_slash_slash;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_schema_slash_slash:
            switch (ch) {
            // 遇到了第二个斜杠，
            case '/':
                r->host_start = p - 1;
                state = sw_host;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_host:
            switch (ch) {
            // 遇到冒号说明host解析完毕，进入解析端口的状态
            case ':':
                r->host_end = p - 1;
                state = sw_port;
                break;
            case '/':
                // 遇到斜杠说明域名解析完毕， 没有端口
                r->host_end = p - 1;
                r->uri_start = p - 1;
                state = sw_after_slash_in_uri;
                break;
            default:
                // 域名的合法字符
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                    || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')
                {
                    break;
                }
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_port:
            switch (ch) {
            case '/':
                r->port_end = p - 1;
                r->uri_start = p - 1;
                state = sw_after_slash_in_uri;
                break;
            default:
                if (ch < '0' && ch > '9') {
                    return NGX_HTTP_PARSE_INVALID_REQUEST;
                }
                break;
            }
            break;

        /* check "/.", "//", and "%" in URI */
        // 解析url路径
        case sw_after_slash_in_uri:
            switch (ch) {
            // 解析路径的时候就遇到CR，说明是0.9版本的http协议
            case CR:
                r->uri_end = p - 1;
                r->http_minor = 9;
                // 还需要一个LF
                state = sw_almost_done;
                break;
            case LF:
                r->uri_end = p - 1;
                r->http_minor = 9;
                state = sw_done;
                break;
            case ' ':
                r->uri_end = p - 1;
                state = sw_http_09;
                break;
            case '.':
            case '%':
                r->complex_uri = 1;
                state = sw_uri;
                break;
            case '/':
                r->complex_uri = 1;
                break;
            case '?':
                r->args_start = p;
                state = sw_uri;
                break;
            default:
                state = sw_check_uri;
                break;
            }
            break;

        /* check "/" and "%" in URI */
        case sw_check_uri:
            switch (ch) {
            case CR:
                r->uri_end = p - 1;
                r->http_minor = 9;
                state = sw_almost_done;
                break;
            case LF:
                r->uri_end = p - 1;
                r->http_minor = 9;
                state = sw_done;
                break;
            case ' ':
                r->uri_end = p - 1;
                state = sw_http_09;
                break;
            case '.':
                r->uri_ext = p;
                break;
            case '/':
                r->uri_ext = NULL;
                state = sw_after_slash_in_uri;
                break;
            case '%':
                r->complex_uri = 1;
                state = sw_uri;
                break;
            case '?':
                r->args_start = p;
                state = sw_uri;
                break;
            }
            break;

        /* URI */
        case sw_uri:
            switch (ch) {
            case CR:
                r->uri_end = p - 1;
                r->http_minor = 9;
                state = sw_almost_done;
                break;
            case LF:
                r->uri_end = p - 1;
                r->http_minor = 9;
                state = sw_done;
                break;
            case ' ':
                r->uri_end = p - 1;
                state = sw_http_09;
                break;
            }
            break;

        /* space+ after URI */
        case sw_http_09:
            switch (ch) {
            case ' ':
                break;
            case CR:
                r->http_minor = 9;
                state = sw_almost_done;
                break;
            case LF:
                r->http_minor = 9;
                state = sw_done;
                break;
            case 'H':
                state = sw_http_H;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_H:
            switch (ch) {
            case 'T':
                state = sw_http_HT;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_HT:
            switch (ch) {
            case 'T':
                state = sw_http_HTT;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_HTT:
            switch (ch) {
            case 'P':
                state = sw_http_HTTP;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        case sw_http_HTTP:
            switch (ch) {
            case '/':
                state = sw_first_major_digit;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        /* first digit of major HTTP version */
        case sw_first_major_digit:
            if (ch < '1' || ch > '9') {
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_major = ch - '0';
            state = sw_major_digit;
            break;

        /* major HTTP version or dot */
        case sw_major_digit:
            if (ch == '.') {
                state = sw_first_minor_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_major = r->http_major * 10 + ch - '0';
            break;

        /* first digit of minor HTTP version */
        case sw_first_minor_digit:
            if (ch < '0' || ch > '9') {
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_minor = ch - '0';
            state = sw_minor_digit;
            break;

        /* minor HTTP version or end of request line */
        case sw_minor_digit:
            if (ch == CR) {
                state = sw_almost_done;
                break;
            }

            if (ch == LF) {
                state = sw_done;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }

            r->http_minor = r->http_minor * 10 + ch - '0';
            break;

        /* end of request line */
        case sw_almost_done:
            r->request_end = p - 2;
            switch (ch) {
            case LF:
                state = sw_done;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_REQUEST;
            }
            break;

        /* suppress warning */
        case sw_done:
            break;
        }
    }
    // 更新位置
    b->pos = p;

    if (state == sw_done) {
        if (r->request_end == NULL) {
            r->request_end = p - 1;
        }

        r->http_version = r->http_major * 1000 + r->http_minor;
        r->state = sw_start;
        // 0.9版本只支持GET
        if (r->http_version == 9 && r->method != NGX_HTTP_GET) {
            return NGX_HTTP_PARSE_INVALID_09_METHOD;
        }

        return NGX_OK;

    } else {
        r->state = state;
        return NGX_AGAIN;
    }
}


ngx_int_t ngx_http_parse_header_line(ngx_http_request_t *r, ngx_buf_t *b)
{
    u_char  c, ch, *p;
    enum {
        sw_start = 0,
        sw_name,
        sw_space_before_value,
        sw_value,
        sw_space_after_value,
        sw_almost_done,
        sw_header_almost_done,
        sw_ignore_line,
        sw_done,
        sw_header_done
    } state;

    state = r->state;
    p = b->pos;

    while (p < b->last && state < sw_done) {
        ch = *p++;

        switch (state) {

        /* first char */
        case sw_start:
            switch (ch) {
            case CR:
                r->header_end = p - 1;
                state = sw_header_almost_done;
                break;
            case LF:
                // 解析完一行header
                r->header_end = p - 1;
                state = sw_header_done;
                break;
            default:
                // 接入解析name状态
                state = sw_name;
                // 指向name的第一个字符
                r->header_name_start = p - 1;
                // name的合法字符
                // 转小写字母
                c = (u_char) (ch | 0x20);
                if (c >= 'a' && c <= 'z') {
                    break;
                }

                if (ch == '-' || ch == '_' || ch == '~' || ch == '.') {
                    break;
                }

                if (ch >= '0' && ch <= '9') {
                    break;
                }

                return NGX_HTTP_PARSE_INVALID_HEADER;

            }
            break;

        /* header name */
        case sw_name:
            c = (u_char) (ch | 0x20);
            if (c >= 'a' && c <= 'z') {
                break;
            }
            // 遇到冒号说明name解析结束
            if (ch == ':') {
                r->header_name_end = p - 1;
                state = sw_space_before_value;
                break;
            }

            if (ch == '-' || ch == '_' || ch == '~' || ch == '.') {
                break;
            }

            if (ch >= '0' && ch <= '9') {
                break;
            }

            /* IIS can send duplicate "HTTP/1.1 ..." lines */
            if (ch == '/'
                && r->proxy
                && p - r->header_start == 5
                && ngx_strncmp(r->header_start, "HTTP", 4) == 0)
            {
                state = sw_ignore_line;
                break;
            }

            return NGX_HTTP_PARSE_INVALID_HEADER;

        /* space* before header value */
        case sw_space_before_value:
            switch (ch) {
            case ' ':
                break;
            case CR:
                r->header_start = r->header_end = p - 1;
                state = sw_almost_done;
                break;
            case LF:
                r->header_start = r->header_end = p - 1;
                state = sw_done;
                break;
            default:
                r->header_start = p - 1;
                state = sw_value;
                break;
            }
            break;

        /* header value */
        case sw_value:
            switch (ch) {
            case ' ':
                r->header_end = p - 1;
                state = sw_space_after_value;
                break;
            case CR:
                r->header_end = p - 1;
                state = sw_almost_done;
                break;
            case LF:
                r->header_end = p - 1;
                state = sw_done;
                break;
            }
            break;

        /* space* before end of header line */
        case sw_space_after_value:
            switch (ch) {
            case ' ':
                break;
            case CR:
                state = sw_almost_done;
                break;
            case LF:
                state = sw_done;
                break;
            default:
                state = sw_value;
                break;
            }
            break;

        /* ignore header line */
        case sw_ignore_line:
            switch (ch) {
            case LF:
                state = sw_start;
                break;
            default:
                break;
            }
            break;

        /* end of header line */
        case sw_almost_done:
            switch (ch) {
            case LF:
                state = sw_done;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_HEADER;
            }
            break;

        /* end of header */
        case sw_header_almost_done:
            switch (ch) {
            case LF:
                state = sw_header_done;
                break;
            default:
                return NGX_HTTP_PARSE_INVALID_HEADER;
            }
            break;

        /* suppress warning */
        case sw_done:
        case sw_header_done:
            break;
        }
    }

    b->pos = p;

    if (state == sw_done) {
        r->state = sw_start;
        return NGX_OK;

    } else if (state == sw_header_done) {
        r->state = sw_start;
        return NGX_HTTP_PARSE_HEADER_DONE;

    } else {
        r->state = state;
        return NGX_AGAIN;
    }
}


ngx_int_t ngx_http_parse_complex_uri(ngx_http_request_t *r)
{
    u_char  c, ch, decoded, *p, *u;
    enum {
        sw_usual = 0,
        sw_slash,
        sw_dot,
        sw_dot_dot,
#if (WIN32)
        sw_dot_dot_dot,
#endif
        sw_quoted,
        sw_quoted_second
    } state, quoted_state;

    decoded = '\0';
    quoted_state = sw_usual;

    state = sw_usual;
    p = r->uri_start;
    u = r->uri.data;
    r->uri_ext = NULL;

    ch = *p++;

    while (p < r->uri_start + r->uri.len + 1) {

        ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "s:%d in:'%x:%c', out:'%c'", state, ch, ch, *u);

        switch (state) {
        case sw_usual:
            switch(ch) {
            case '/':
                r->uri_ext = NULL;
                state = sw_slash;
                *u++ = ch;
                break;
            case '%':
                quoted_state = state;
                state = sw_quoted;
                break;
            case '.':
                r->uri_ext = u + 1;
            default:
                *u++ = ch;
                break;
            }
            ch = *p++;
            break;

        case sw_slash:
            switch(ch) {
            case '/':
                break;
            case '.':
                state = sw_dot;
                *u++ = ch;
                break;
            case '%':
                quoted_state = state;
                state = sw_quoted;
                break;
            default:
                state = sw_usual;
                *u++ = ch;
                break;
            }
            ch = *p++;
            break;

        case sw_dot:
            switch(ch) {
            case '/':
                state = sw_slash;
                u--;
                break;
            case '.':
                state = sw_dot_dot;
                *u++ = ch;
                break;
            case '%':
                quoted_state = state;
                state = sw_quoted;
                break;
            default:
                state = sw_usual;
                *u++ = ch;
                break;
            }
            ch = *p++;
            break;

        case sw_dot_dot:
            switch(ch) {
            case '/':
                state = sw_slash;
                u -= 4;
                if (u < r->uri.data) {
                    return NGX_HTTP_PARSE_INVALID_REQUEST;
                }
                while (*(u - 1) != '/') {
                    u--;
                }
                break;
            case '%':
                quoted_state = state;
                state = sw_quoted;
                break;
#if (WIN32)
            case '.':
                state = sw_dot_dot_dot;
                *u++ = ch;
                break;
#endif
            default:
                state = sw_usual;
                *u++ = ch;
                break;
            }
            ch = *p++;
            break;

#if (WIN32)
        case sw_dot_dot_dot:
            switch(ch) {
            case '/':
                state = sw_slash;
                u -= 5;
                if (u < r->uri.data) {
                    return NGX_HTTP_PARSE_INVALID_REQUEST;
                }
                while (*u != '/') {
                    u--;
                }
                if (u < r->uri.data) {
                    return NGX_HTTP_PARSE_INVALID_REQUEST;
                }
                while (*(u - 1) != '/') {
                    u--;
                }
                break;
            case '%':
                quoted_state = state;
                state = sw_quoted;
                break;
            default:
                state = sw_usual;
                *u++ = ch;
                break;
            }
            ch = *p++;
            break;
#endif

        case sw_quoted:
            if (ch >= '0' && ch <= '9') {
                decoded = (u_char) (ch - '0');
                state = sw_quoted_second;
                ch = *p++;
                break;
            }

            c = (u_char) (ch | 0x20);
            if (c >= 'a' && c <= 'f') {
                decoded = (u_char) (c - 'a' + 10);
                state = sw_quoted_second;
                ch = *p++;
                break;
            }

            return NGX_HTTP_PARSE_INVALID_REQUEST;

        case sw_quoted_second:
            if (ch >= '0' && ch <= '9') {
                ch = (u_char) ((decoded << 4) + ch - '0');
                if (ch == '%') {
                    state = sw_usual;
                    *u++ = ch;
                    ch = *p++;
                    break;
                }
                state = quoted_state;
                break;
            }

            c = (u_char) (ch | 0x20);
            if (c >= 'a' && c <= 'f') {
                ch = (u_char) ((decoded << 4) + c - 'a' + 10);
                if (ch == '%') {
                    state = sw_usual;
                    *u++ = ch;
                    ch = *p++;
                    break;
                }
                state = quoted_state;
                break;
            }

            return NGX_HTTP_PARSE_INVALID_REQUEST;
        }
    }

    r->uri.len = u - r->uri.data;
    r->uri.data[r->uri.len] = '\0';

    if (r->uri_ext) {
        r->exten.len = u - r->uri_ext;

        if (!(r->exten.data = ngx_palloc(r->pool, r->exten.len + 1))) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ngx_cpystrn(r->exten.data, r->uri_ext, r->exten.len + 1);
    }

    r->uri_ext = NULL;

    return NGX_OK;
}
