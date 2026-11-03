#pragma once

#include <stdarg.h>

/** GhostLock-compatible levels: V/D/I/W/E. */
typedef enum df_log_level {
    DF_LOG_VERBOSE = 'V',
    DF_LOG_DEBUG = 'D',
    DF_LOG_INFO = 'I',
    DF_LOG_WARN = 'W',
    DF_LOG_ERROR = 'E',
} df_log_level;

/** Set minimum level; default I. */
/** Write one level-tagged line to stdout (captured by ExploitRunner). */
void df_log_write(df_log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define DF_LOGV(...) df_log_write(DF_LOG_VERBOSE, __VA_ARGS__)
#define DF_LOGD(...) df_log_write(DF_LOG_DEBUG, __VA_ARGS__)
#define DF_LOGI(...) df_log_write(DF_LOG_INFO, __VA_ARGS__)
#define DF_LOGW(...) df_log_write(DF_LOG_WARN, __VA_ARGS__)
#define DF_LOGE(...) df_log_write(DF_LOG_ERROR, __VA_ARGS__)
