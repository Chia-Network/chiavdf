#include "hw_util.hpp"

#include <cstdarg>
#include <ctime>

void vdf_do_log(const char *msg, ...)
{
    va_list ap;
    struct tm cal;
    char time_str[25];
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &cal);
    strftime(time_str, sizeof(time_str), "%FT%T", &cal);
    fprintf(stderr, "%s.%03ld ", time_str, ts.tv_nsec / 1000000);

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
}
