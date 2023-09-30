#ifndef HW_UTIL_H
#define HW_UTIL_H

#include <chrono>
#include <cstdio>

void vdf_do_log(const char *msg, ...);

#ifndef VDF_ENABLE_LOG_DEBUG
# define VDF_ENABLE_LOG_DEBUG 0
#endif

#if VDF_ENABLE_LOG_DEBUG
# define LOG_DEBUG(msg, ...) vdf_do_log(msg "\n", ##__VA_ARGS__)
#else
# define LOG_DEBUG(msg, ...) ((void)msg)
#endif
#define LOG_INFO(msg, ...) vdf_do_log(msg "\n", ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) vdf_do_log(msg "\n", ##__VA_ARGS__)

#define LOG_SIMPLE(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)

typedef std::chrono::time_point<std::chrono::high_resolution_clock> timepoint_t;

static inline timepoint_t vdf_get_cur_time(void)
{
    return std::chrono::high_resolution_clock::now();
}

static inline uint64_t vdf_get_elapsed_us(timepoint_t &t1)
{
    auto t2 = vdf_get_cur_time();
    return std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
}

#endif /* HW_UTIL_H */
