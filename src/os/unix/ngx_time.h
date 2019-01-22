
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_TIME_H_INCLUDED_
#define _NGX_TIME_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef uint64_t       ngx_epoch_msec_t;

typedef ngx_int_t      ngx_msec_t;

// struct tm {
// int tm_sec; //代表目前秒数，正常范围为0-59，但允许至61秒 
// int tm_min; //代表目前分数，范围0-59
// int tm_hour; //从午夜算起的时数，范围为0-23
// int tm_mday; //目前月份的日数，范围1-31
// int tm_mon; //代表目前月份，从一月算起，范围从0-11
// int tm_year; //从1900 年算起至今的年数,其值等于实际年份减去1900
// int tm_wday; //一星期的日数，从星期一算起，范围为0-6 ,其中0代表星期天，1代表星期一，以此类推
// int tm_yday; //从今年1月1日算起至今的天数，范围为0-365
// int tm_isdst; //日光节约时间的旗标,实行夏令时的时候，tm_isdst为正,不实行夏令时的时候，tm_isdst为0，不了解情况时，tm_isdst()为负。
// };

typedef struct tm      ngx_tm_t;

#define ngx_tm_sec     tm_sec
#define ngx_tm_min     tm_min
#define ngx_tm_hour    tm_hour
#define ngx_tm_mday    tm_mday
#define ngx_tm_mon     tm_mon
#define ngx_tm_year    tm_year
#define ngx_tm_wday    tm_wday
#define ngx_tm_isdst   tm_isdst

#define ngx_tm_sec_t   int
#define ngx_tm_min_t   int
#define ngx_tm_hour_t  int
#define ngx_tm_mday_t  int
#define ngx_tm_mon_t   int
#define ngx_tm_year_t  int
#define ngx_tm_wday_t  int


#if (HAVE_GMTOFF)
#define ngx_tm_gmtoff  tm_gmtoff
#define ngx_tm_zone    tm_zone
#endif


#if (SOLARIS)
#define ngx_timezone(isdst) (- (isdst ? altzone : timezone) / 60)
#endif


void ngx_localtime(ngx_tm_t *tm);

#define ngx_gettimeofday(tp)  gettimeofday(tp, NULL);
#define ngx_msleep(ms)        usleep(ms * 1000)


#endif /* _NGX_TIME_H_INCLUDED_ */
