#include "df_log.h"

#include <stdio.h>
#include <string.h>

static const df_log_level g_min = DF_LOG_INFO;

void df_log_write(df_log_level level, const char *fmt, ...) {
    if ((int)level < (int)g_min) return;
    flockfile(stdout);
    fprintf(stdout, "<%c> [NATIVE] ", (char)level);
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    const size_t n = strlen(fmt);
    if (n == 0 || fmt[n - 1] != '\n') fputc('\n', stdout);
    fflush(stdout);
    funlockfile(stdout);
}
