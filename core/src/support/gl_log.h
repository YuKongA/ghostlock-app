#ifndef GHOSTLOCK_GL_LOG_H
#define GHOSTLOCK_GL_LOG_H
/**
 *  @file support/gl_log.h
 *  最小的日志系统（2026-10-18 用户裁定：**纯 C**）：printf 语义 + 三条去处。
 *
 *  - logcat（目标侧）：`file:line 正文`，`__android_log_print`；
 *  - 应用打开的 fd（目标侧）：`[级别符号 HH:mm:ss.mmm] 正文`，一行一次 write(2)；
 *  - stderr（宿主侧）：同上 `file:line 正文`。
 *
 *  用法：info("open path=%s role=%d", path, role);
 *  @note 只允许 ASCII 格式串；printf 语义（%s/%d/%u/%zu/%p/%02x …）。
 *  @note 状态按 **DSO** 各一份：实现编进静态库后分别链进三个 JNI 库，各自持有自己的 fd。
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
 *  注册导出 fd（由各库的 JNI 入口调用）。
 *
 *  @param fd 应用以 O_APPEND 打开的追加写 fd；-1 表示注销（退回只走 logcat）。
 *  @return 上一次注册的 fd（-1＝之前没有）。
 *  @note 宿主侧是空操作（日志导出是设备上的事）。
 */
int gl_log_set_export_fd(int fd);

/** 当前注册的导出 fd（-1＝未注册）。 */
int gl_log_export_fd(void);

/** 设置阈值：低于它的级别一律丢弃。 */
void gl_log_set_min_level(gl_log_level level);

/** 当前阈值。 */
gl_log_level gl_log_min_level(void);

/**
 *  时间戳开关（2026-10-18 用户裁定：debug 构建显示“启动后毫秒数”，release 不显示）。
 *
 *  @param enabled 非 0 = 行里带 `1234ms`；0 = 不带。默认开启（宿主调试方便）。
 *  @note 由 Kotlin 在注册日志 fd 时按 BuildConfig.DEBUG 传入（见 NativeLogBridge）。
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
 *  @param level 级别；高于阈值直接返回（不格式化、不写任何去处）。
 *  @param file  调用点文件（宏传 __FILE__）。
 *  @param line  调用点行号（宏传 __LINE__）。
 *  @param fmt   ASCII 格式串。
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
