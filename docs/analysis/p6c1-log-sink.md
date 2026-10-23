# P6c-1 证据：GlkFdSink / glk_logf（2026-10-21）

> 设计：[p6c-log-sink-design](p6c-log-sink-design.md) 的 P6c-1 子批。
> 本批只新增实现，不改任何日志调用点。

## 新增

`app/src/main/cpp/glk_log/glk_log.h/.c`

- `GlkLogSink` 从 43499 session 头移到公共头。
- `GlkFdSink { fd, kind, disabled, in_pi }`。
- `glk_fd_sink_init`：fstat/getsockopt 判定 FILE/SOCKET/FIFO；
  `disabled = fd < 0`。
- `glk_fd_sink_close`：置 disabled、fd=-1、close 一次。
- `glk_fd_sink_write`：
  - SOCKET：`send(MSG_NOSIGNAL)`
  - FIFO：block SIGPIPE → write → 零超时 `sigtimedwait` 消费 EPIPE → 恢复 mask
  - FILE：write
  - 失败只置 disabled，不 close
- `glk_logf`：512 B 栈缓冲，超长保留 `...`，单次 sink->write。
- `glk_log_pi_enter/exit`：仅对 `glk_fd_sink_write` 的 sink 设置 `in_pi`。

CMake 将 `glk_log/glk_log.c` 编入 43499/43284 两个 SHARED。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

ghostlock43499 b/B = 0
ghostlock43284 b/B = 0
```

## Host 测试

`tools/tests/glk_log_test.c`：

```text
clang -std=c11 -Wall -Wextra -Werror -I app/src/main/cpp/glk_log \
  tools/tests/glk_log_test.c app/src/main/cpp/glk_log/glk_log.c \
  -pthread -o /tmp/glk_log_test
/tmp/glk_log_test  # rc=0
```

覆盖：

- FILE 写入 + 读回
- 700B 输入截断到 <512B，末尾保留 `...`
- FIFO reader 关闭 + SIGPIPE ignored：返回、disabled，不阻塞

注意：macOS 无 `sigtimedwait`，测试走 `#else` 分支；
NDK/Android 的 Linux 分支仍需真机或 emulator 覆盖。

## 未完成

- P6c-2：43499 调用点接入；P6c-3：43284；P6c-4：JNI create(fd)+真机。
- pre-session 非文件日志仍未取得用户接受。

## 文档

- [P6c 设计](p6c-log-sink-design.md)
- [P6b](p6b-jni-session.md)
