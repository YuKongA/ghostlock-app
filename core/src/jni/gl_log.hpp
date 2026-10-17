#pragma once
/** @file jni/gl_log.hpp
 *  最小的日志系统：define + printf（不用 spdlog）。目标侧走 __android_log_print 落到 logcat，
 *  宿主侧走 stderr。用途：让 native 每个决策点都能被看见 —— 这是排障的前提，不是装饰。
 *
 *  用法：GL_LOGI("open path=%s role=%d", path, role);
 *  @note 只允许 ASCII 格式串；printf 语义（%s/%d/%u/%zu/%p/%02x …）。
 *        换行一律用码值 10（fputc(10, …)），避免任何转义歧义。
 *
 *  **第二份去处（2026-10-16 用户裁定：屏幕上的日志必须与 adb 的一样）**：目标侧除 logcat 外，
 *  再往**应用打开的文件描述符**写一行同内容（本地时间戳 + N + 等级 + 模块:行号 + 正文，例如
 *  `20:51:02.123 N I pagecache_steps.cpp:285 open path=…`）。fd 由 Kotlin 在启动时经 JNI 注册
 *  （com.ghostlock.app.log.NativeLogBridge）：**路径、轮转、开关都在 Kotlin**，本文件只按格式写
 *  一次并 flush —— write(2) 直接进内核，没有用户态缓冲，读者立刻可见，所以不需要 fsync
 *  （每行一次 fsync 会把排障日志变成性能问题）。
 *  @note 注册按**库**各一份：内联函数的函数内静态量随 DSO 复制，所以三个 JNI 库各有一个 JNI 入口，
 *        Kotlin 侧逐个注册（见 NativeLogBridge）。
 */
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#include <ctime>
#include <unistd.h>
#endif

namespace ghostlock::log {
    inline constexpr const char *kTag = "GhostLockNative";

#if defined(__ANDROID__)
    namespace detail {
        /**
         *  导出 fd 的唯一位点（-1＝未注册）。
         *
         *  @note 形态是**内联函数内的静态量**（每个 DSO 一份），不是命名空间级全局：本头文件只有
         *        这一个可变量，写只有 set_export_fd 一个入口，读只有 export_fd 一个出口。
         */
        inline std::atomic<int> &export_slot() noexcept {
            static std::atomic<int> slot{-1};
            return slot;
        }

        /** 本地时间 HH:MM:SS.mmm（与 Kotlin 侧同一格式：两边的行可以直接对拍）。 */
        inline void stamp_now(char *out, const std::size_t capacity) noexcept {
            timespec now{};
            if (::clock_gettime(CLOCK_REALTIME, &now) != 0) {
                std::snprintf(out, capacity, "--:--:--.---");
                return;
            }
            const std::time_t seconds = now.tv_sec;
            std::tm parts{};
            if (::localtime_r(&seconds, &parts) == nullptr) {
                std::snprintf(out, capacity, "--:--:--.---");
                return;
            }
            std::snprintf(out, capacity, "%02d:%02d:%02d.%03d", parts.tm_hour, parts.tm_min,
                          parts.tm_sec, static_cast<int>(now.tv_nsec / 1000000));
        }
    }  // namespace detail
#endif

    /**
     *  注册导出 fd（由各库的 JNI 入口调用）。
     *
     *  @param fd 应用以 O_APPEND 打开的追加写 fd；-1 表示注销（退回只走 logcat）。
     *  @return 上一次注册的 fd（-1＝之前没有）。
     *  @note 仅在目标侧生效；宿主侧是空操作（日志导出是设备上的事）。
     */
    inline int set_export_fd(const int fd) noexcept {
#if defined(__ANDROID__)
        return detail::export_slot().exchange(fd);
#else
        static_cast<void>(fd);
        return -1;
#endif
    }

    /** 当前注册的导出 fd（-1＝未注册）。 */
    [[nodiscard]] inline int export_fd() noexcept {
#if defined(__ANDROID__)
        return detail::export_slot().load();
#else
        return -1;
#endif
    }

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
        /* 第二份去处：应用打开的文件描述符（未注册 ⇒ 只走 logcat，与改造前的行为一致）。 */
        const int fd = export_fd();
        if (fd >= 0) {
            char stamp[16] = {};
            detail::stamp_now(stamp, sizeof(stamp));
            char exported[768] = {};
            std::snprintf(exported, sizeof(exported), "%s N I %s:%d %s\n", stamp, slash, line, body);
            const std::size_t want = std::strlen(exported);
            std::size_t done = 0U;
            while (done < want) {
                const ssize_t wrote = ::write(fd, exported + done, want - done);
                if (wrote <= 0) {
                    break; /* 日志写失败绝不影响主流程。 */
                }
                done += static_cast<std::size_t>(wrote);
            }
        }
#else
        std::fputs(full, stderr);
        std::fputc(10, stderr);
#endif
    }
}  // namespace ghostlock::log

#define GL_LOGI(...) ::ghostlock::log::write(__FILE__, __LINE__, __VA_ARGS__)
