/*
 * glk98260 JNI 机械面（B2）：只承担 UserService 进程内无法用 Kotlin 安全完成的两件事：
 *   1) posix_spawn + POSIX_SPAWN_SETSID 启动独立 payload 进程（stdout/stderr 重定向到日志文件）；
 *   2) 对**非 parked** 子进程做有界 waitExit（waitpid WNOHANG 轮询）。
 *
 * 边界（delta §2，N-32）：
 *   - 不提供 kill/signal/fork/system/execve 入口；app/service 任何路径不得向 payload 发信号。
 *   - 文件读写/预检/日志轮询在 Kotlin 侧完成（File/proc），本 DSO 不做业务判断。
 *   - 不用 `-Wl,--wrap=fork/vfork`（该零 fork 证明只属 43499 app-JVM 路径）。
 *   - JNI 不抛 Java 异常：错误以负数/具名码返回（-errno / -1003 超时）。
 *
 * 阶段：B2；真机门禁 G8/G9 未过（device-gate-pending）。
 */
#define _GNU_SOURCE

#include <jni.h>

#include "common/gl_log.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;

/** JNI 机械面 ABI 版本；Kotlin 侧启动时校验，不匹配 ⇒ 具名拒绝。 */
#define GLK98260_JNI_API_VERSION 1

/** waitExit 超时返回码（与 Kotlin 侧一致；不发任何信号）。 */
#define GLK98260_JNI_TIMEOUT (-1003)

/**
 * @brief 返回 JNI 机械面 ABI 版本。
 * @return GLK98260_JNI_API_VERSION；Kotlin `Native98260Jni.API_VERSION` 必须相等。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native98260Jni_nativeApiVersion(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return (jint)GLK98260_JNI_API_VERSION;
}

/**
 * @brief 深拷贝 Java String 到调用方缓冲（fail-closed）。
 * @return 0=成功；负 errno=失败（缓冲内容未定义）。
 */
static int copy_jstring(JNIEnv *env, jstring src, char *dst, size_t cap)
{
    const char *utf;
    size_t n;

    if (src == NULL || dst == NULL || cap == 0U)
    {
        return -EINVAL;
    }
    utf = (*env)->GetStringUTFChars(env, src, NULL);
    if (utf == NULL)
    {
        return -ENOMEM;
    }
    n = strlen(utf);
    if (n >= cap)
    {
        (*env)->ReleaseStringUTFChars(env, src, utf);
        return -ENAMETOOLONG;
    }
    memcpy(dst, utf, n + 1U);
    (*env)->ReleaseStringUTFChars(env, src, utf);
    return 0;
}

/**
 * @brief 深拷贝 Java String[] 到 NULL 结尾 argv（fail-closed；上限 32 项/每项 1024 B）。
 * @param out 输出 argv（calloc；调用方 free）。
 * @return 0=成功；负 errno=失败。
 */
static int copy_argv(JNIEnv *env, jobjectArray src, char ***out)
{
    jsize n;
    char **argv;

    if (src == NULL || out == NULL)
    {
        return -EINVAL;
    }
    n = (*env)->GetArrayLength(env, src);
    if (n <= 0 || n > 32)
    {
        return -EINVAL;
    }
    argv = (char **)calloc((size_t)n + 1U, sizeof(char *));
    if (argv == NULL)
    {
        return -ENOMEM;
    }
    for (jsize i = 0; i < n; i++)
    {
        jstring item = (jstring)(*env)->GetObjectArrayElement(env, src, i);
        const char *utf;
        size_t len;

        if (item == NULL)
        {
            goto fail;
        }
        utf = (*env)->GetStringUTFChars(env, item, NULL);
        if (utf == NULL)
        {
            (*env)->DeleteLocalRef(env, item);
            goto fail;
        }
        len = strlen(utf);
        if (len == 0U || len > 1024U)
        {
            (*env)->ReleaseStringUTFChars(env, item, utf);
            (*env)->DeleteLocalRef(env, item);
            goto fail;
        }
        argv[i] = (char *)malloc(len + 1U);
        if (argv[i] == NULL)
        {
            (*env)->ReleaseStringUTFChars(env, item, utf);
            (*env)->DeleteLocalRef(env, item);
            goto fail;
        }
        memcpy(argv[i], utf, len + 1U);
        (*env)->ReleaseStringUTFChars(env, item, utf);
        (*env)->DeleteLocalRef(env, item);
    }
    *out = argv;
    return 0;

fail:
    for (jsize i = 0; i < n; i++)
    {
        free(argv[i]);
    }
    free(argv);
    return -EINVAL;
}

static void free_argv(char **argv, jsize n)
{
    for (jsize i = 0; i < n; i++)
    {
        free(argv[i]);
    }
    free(argv);
}

/**
 * @brief posix_spawn 一个独立 payload 进程（新会话；stdout/stderr 写日志文件）。
 *
 * @param path payload 绝对路径（/data/local/tmp/...；不得为空）。
 * @param argv argv[0..n)；NULL 结尾由本函数负责。
 * @param outPath 日志文件路径（O_CREAT|O_TRUNC）；父进程持有 fd 后交给子进程再关闭。
 * @return >0=pid；<=0=-errno（spawn 失败）；Kotlin 侧据此具名拒绝（不 kill/不重试）。
 * @note 子进程 stdin=/dev/null；`setsid` 由 POSIX_SPAWN_SETSID 完成；无 fork()/execve() 调用。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native98260Jni_spawnStatic(
    JNIEnv *env, jclass cls, jstring path, jobjectArray argv, jstring outPath)
{
    char pathBuf[4096];
    char outBuf[4096];
    char **args = NULL;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    pid_t pid = 0;
    int rc;
    int outFd = -1;
    jsize argc;

    (void)cls;
    if (path == NULL || argv == NULL || outPath == NULL)
    {
        return -EINVAL;
    }
    rc = copy_jstring(env, path, pathBuf, sizeof(pathBuf));
    if (rc != 0)
    {
        return rc;
    }
    rc = copy_jstring(env, outPath, outBuf, sizeof(outBuf));
    if (rc != 0)
    {
        return rc;
    }
    rc = copy_argv(env, argv, &args);
    if (rc != 0)
    {
        return rc;
    }
    argc = (*env)->GetArrayLength(env, argv);
    outFd = open(outBuf, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (outFd < 0)
    {
        free_argv(args, argc);
        return -errno;
    }
    if (posix_spawn_file_actions_init(&actions) != 0)
    {
        close(outFd);
        free_argv(args, argc);
        return -EINVAL;
    }
    if (posix_spawnattr_init(&attr) != 0)
    {
        posix_spawn_file_actions_destroy(&actions);
        close(outFd);
        free_argv(args, argc);
        return -EINVAL;
    }
    /* 子进程 fd：stdin=/dev/null、stdout/stderr=日志文件；其余由 CLOEXEC 限制。 */
    rc = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                         O_RDONLY, 0);
    if (rc == 0)
    {
        rc = posix_spawn_file_actions_adddup2(&actions, outFd, STDOUT_FILENO);
    }
    if (rc == 0)
    {
        rc = posix_spawn_file_actions_adddup2(&actions, outFd, STDERR_FILENO);
    }
    if (rc == 0)
    {
        rc = posix_spawn_file_actions_addclose(&actions, outFd);
    }
    if (rc == 0)
    {
        /* 继承 fd 纪律（R1e）：CLOEXEC_DEFAULT 关闭除 0/1/2 与显式 dup2 外的全部 fd，
         * 避免 payload 继承 service/binder fds；SETSID 让 payload 独立会话（不随 service 退出）。 */
        short flags = (short)(POSIX_SPAWN_SETSID);
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
        flags = (short)(flags | POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
        (void)posix_spawnattr_setflags(&attr, flags);
#if defined(POSIX_SPAWN_SETSIGDEF)
        {
            sigset_t def = {0};
            sigemptyset(&def);
            (void)posix_spawnattr_setsigdefault(&attr, &def);
        }
#endif
        rc = posix_spawn(&pid, pathBuf, &actions, &attr, args, environ);
    }
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    close(outFd);
    free_argv(args, argc);
    if (rc != 0)
    {
        return -rc;
    }
    return (jint)pid;
}

/**
 * @brief 有界等待子进程退出（WNOHANG 轮询；**不发信号**）。
 *
 * @param pid spawnStatic 返回的 pid。
 * @param timeoutMs 等待窗口（≤600000）。
 * @return 子进程退出码（>=0，信号终止=128+sig）；GLK98260_JNI_TIMEOUT=窗口耗尽；
 *         其余负值=-errno。**超时后调用方不得 kill**（parked payload 按 D6 处理）。
 * @note 只用于 verify 类短命令；payload 主进程可能永久 park，禁止对其调用。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native98260Jni_waitExit(
    JNIEnv *env, jclass cls, jint pid, jint timeoutMs)
{
    struct timespec begin;
    int status = 0;

    (void)env;
    (void)cls;
    if (pid <= 0 || timeoutMs <= 0 || timeoutMs > 600000)
    {
        return -EINVAL;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &begin) != 0)
    {
        return -errno;
    }
    for (;;)
    {
        pid_t r = waitpid((pid_t)pid, &status, WNOHANG);
        struct timespec now;
        long elapsedMs;

        if (r == (pid_t)pid)
        {
            if (WIFEXITED(status))
            {
                return (jint)WEXITSTATUS(status);
            }
            if (WIFSIGNALED(status))
            {
                return (jint)(128 + WTERMSIG(status));
            }
            return -ECHILD;
        }
        if (r < 0 && errno != EINTR)
        {
            return -errno;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        {
            return -errno;
        }
        elapsedMs = (long)(now.tv_sec - begin.tv_sec) * 1000L +
                    (long)(now.tv_nsec - begin.tv_nsec) / 1000000L;
        if (elapsedMs >= (long)timeoutMs)
        {
            return GLK98260_JNI_TIMEOUT;
        }
        {
            struct timespec pause = {0, 5L * 1000000L};
            (void)nanosleep(&pause, NULL);
        }
    }
}

/**
 * @brief 接上 app 日志 fd（项目 gl_log 管线；内部 dup+CLOEXEC）。
 *
 * @param fd service 从 ParcelFileDescriptor.detachFd() 得到的 raw fd（>0）；**本函数接管并 close**；
 *           gl_log 持有自己的 dup+CLOEXEC。
 * @return 无；失败静默（日志失败不影响攻击）。
 * @note 只写日志；不参与控制/数据通道；不转发/不落 payload raw。
 */
JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_Native98260Jni_attachLogFd(
    JNIEnv *env, jclass cls, jint fd)
{
    (void)env;
    (void)cls;
    if (fd < 0)
    {
        return;
    }
    (void)gl_log_attach_fd((int)fd, 1, GL_LOG_INFO);
    /* caller 交所有权（Kotlin detachFd 后不再 close）；gl_log 已 dup+CLOEXEC。 */
    (void)close((int)fd);
}

/**
 * @brief 断开 app 日志 fd（destroy 时调用；关闭 gl_log 持有的 dup）。
 */
JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_Native98260Jni_detachLogFd(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    (void)gl_log_attach_fd(-1, 1, GL_LOG_INFO);
}

/**
 * @brief 写一行到项目日志管线（gl_log：logcat + app fd）。
 *
 * @param level 0=verbose..4=error（越界按 info 处理）。
 * @param msg UTF-8 单行正文（不含密钥；调用方保证）。
 * @note 一行一次 write；失败不影响攻击。
 */
JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_Native98260Jni_logLine(
    JNIEnv *env, jclass cls, jint level, jstring msg)
{
    const char *text;
    gl_log_level mapped;

    (void)cls;
    text = (*env)->GetStringUTFChars(env, msg, NULL);
    if (text == NULL)
    {
        return;
    }
    switch (level)
    {
    case 0: mapped = GL_LOG_VERBOSE; break;
    case 1: mapped = GL_LOG_DEBUG; break;
    case 2: mapped = GL_LOG_INFO; break;
    case 3: mapped = GL_LOG_WARN; break;
    case 4: mapped = GL_LOG_ERROR; break;
    default: mapped = GL_LOG_INFO; break;
    }
    gl_log_write(mapped, __FILE__, __LINE__, "%s", text);
    (*env)->ReleaseStringUTFChars(env, msg, text);
}
