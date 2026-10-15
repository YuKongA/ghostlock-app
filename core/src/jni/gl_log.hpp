#pragma once
/** @file jni/gl_log.hpp
 *  最小的日志系统：define + printf（不用 spdlog）。目标侧走 __android_log_print 落到 logcat，
 *  宿主侧走 stderr。用途：让 native 每个决策点都能被看见 —— 这是排障的前提，不是装饰。
 *
 *  用法：GL_LOGI("open path=%s role=%d", path, role);
 *  @note 只允许 ASCII 格式串；printf 语义（%s/%d/%u/%zu/%p/%02x …）。
 *        换行一律用码值 10（fputc(10, …)），避免任何转义歧义。
 */
#include <cstdarg>
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace ghostlock::log {
    inline constexpr const char *kTag = "GhostLockNative";

    inline void write(const char *file, const int line, const char *fmt, ...) noexcept {
        const char *slash = file;
        for (const char *cursor = file; *cursor != 0; ++cursor) {
            if (*cursor == '/') {
                slash = cursor + 1;
            }
        }
        char body[512] = {};
        va_list args;
        va_start(args, fmt);
        const int written = std::vsnprintf(body, sizeof(body), fmt, args);
        va_end(args);
        if (written < 0) {
            return;
        }
        char full[640] = {};
        std::snprintf(full, sizeof(full), "%s:%d %s", slash, line, body);
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO, kTag, "%s", full);
#else
        std::fputs(full, stderr);
        std::fputc(10, stderr);
#endif
    }
}  // namespace ghostlock::log

#define GL_LOGI(...) ::ghostlock::log::write(__FILE__, __LINE__, __VA_ARGS__)
