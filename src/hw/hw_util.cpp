#include "hw_util.hpp"

#include <cstdarg>
#include <ctime>

void vdf_do_log(const char *msg, ...)
{
    va_list ap;
    struct tm cal;
    char time_str[25];
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - secs).count();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);

#ifdef _WIN32
    localtime_s(&cal, &tt);
#else
    localtime_r(&tt, &cal);
#endif
    strftime(time_str, sizeof(time_str), "%FT%T", &cal);
    fprintf(stderr, "%s.%03lld ", time_str, static_cast<long long>(millis));

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
}
