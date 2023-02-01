#ifndef HW_UTIL_H
#define HW_UTIL_H

#ifndef VDF_ENABLE_LOG_DEBUG
# define VDF_ENABLE_LOG_DEBUG 0
#endif

#if VDF_ENABLE_LOG_DEBUG
# define LOG_DEBUG(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)
#else
# define LOG_DEBUG(msg, ...) ((void)msg)
#endif
#define LOG_INFO(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)

#endif /* HW_UTIL_H */
