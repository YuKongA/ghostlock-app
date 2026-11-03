#ifndef GHOSTLOCK_GL_LOG_H
#define GHOSTLOCK_GL_LOG_H
/**
 *  @file common/gl_log.h
 *  最小的日志系统（2026-10-18 用户裁定：**纯 C**）：printf 语义 + 三条去处。
 *
 *  - logcat（目标侧）：`file:line 正文`，priority 与 V/D/I/W/E 一一对应；
 *  - 导出 fd（目标侧）：`[级别/启动毫秒] 正文`，ASCII 单行且每条最多一次 write(2)；
 *  - stderr（宿主侧）：同一等级/时间格式，并包含 `file:line`。
 *
 *  用法：info("open path=%s role=%d", path, role);
 *  @note 控制字节折叠为空格、非 ASCII 字节替换为 '?'; printf 语义（%s/%d/%u/%zu/%p/%02x …）。
 *  @note 状态按 DSO/可执行文件各一份；该库分别被 JNI DSO 与 standalone executable 链接。
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(__ANDROID__)
#include <android/log.h>
#include <time.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** logcat 标签。 */
#define GL_LOG_TAG "GhostLockNative"

/** 级别：数值越大越粗；默认阈值 Info。 */
typedef enum gl_log_level {
    GL_LOG_VERBOSE = 0,
    GL_LOG_DEBUG = 1,
    GL_LOG_INFO = 2,
    GL_LOG_WARN = 3,
    GL_LOG_ERROR = 4
} gl_log_level;

/**
 *  注册导出 fd（由 JNI 入口或 standalone executable 初始化）。
 *
 *  @param fd 应用以 O_APPEND 打开的追加写 fd；-1 表示注销（退回只走 logcat）。
 *  @return 上一次注册的 fd（-1＝之前没有）。
 *  @note 该 fd 不转移给 logger；传入 -1 只关闭导出通道，不影响 Android logcat。
 */
int gl_log_set_export_fd(int fd);

/**
 *  @brief Adopt a duplicate of an append-only log fd.
 *  @param fd Output fd; <0 disables file export.
 *  @param timestamps Legacy compatibility value; records always include elapsed milliseconds.
 *  @param min_level Minimum level for all sinks.
 *  @return The owned duplicate fd, or -1 when disabled/dup failed.
 *  @note The previous owned fd is closed. The caller keeps its original fd; output failures are ignored.
 */
int gl_log_attach_fd(int fd, int timestamps, gl_log_level min_level);

/** 当前注册的导出 fd（-1＝未注册）。 */
int gl_log_export_fd(void);

/** 设置阈值：低于它的级别一律丢弃。 */
void gl_log_set_min_level(gl_log_level level);

/** 当前阈值。 */
gl_log_level gl_log_min_level(void);

/**
 *  Legacy timestamp controls retained for source compatibility; elapsed milliseconds are always emitted.
 *
 *  @param enabled Ignored compatibility argument.
 *  @note Output format always follows the project's `[level/elapsed-ms]` rule.
 */
void gl_log_set_timestamps(int enabled);
int gl_log_timestamps(void);

/**
 *  名字 → 级别（"trace"/"debug"/"info"/"warn"/"error"/"critical"/"off"）。
 *  认不出来按 info 处理（不猜成 off，避免静默丢日志）。
 */
gl_log_level gl_log_level_from_name(const char *name);

/**
 *  写一行（printf 语义）。
 *
 *  @param level 级别；低于阈值直接返回（不格式化、不写任何去处）。
 *  @param file  调用点文件（宏传 __FILE__）。
 *  @param line  调用点行号（宏传 __LINE__）。
 *  @param fmt   printf 格式串；输出端规整为单行 ASCII。
 *  @note Sink 失败、partial write 与 EINTR 均丢弃；每个 fd sink 最多调用一次 write(2)。
 */
void gl_log_write(gl_log_level level, const char *file, int line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#define verbose(...) gl_log_write(GL_LOG_VERBOSE, __FILE__, __LINE__, __VA_ARGS__)
#define debug(...) gl_log_write(GL_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define info(...) gl_log_write(GL_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define warn(...) gl_log_write(GL_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define error(...) gl_log_write(GL_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* GHOSTLOCK_GL_LOG_H */
