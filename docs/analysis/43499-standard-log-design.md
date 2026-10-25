# 43499 标准日志库迁移（2026-10-22）

## 已实施

`common.h` 在 `kernelsnitch/utils.h` 之后 include `common/gl_log.h`，
并重定义活动 TU 的 `pr_*`：

```c
#undef pr_info / pr_success / pr_warning / pr_error
pr_info    -> gl_log_write(GL_LOG_INFO, __FILE__, __LINE__, ...)
pr_success -> gl_log_write(GL_LOG_INFO, __FILE__, __LINE__, ...)
pr_warning -> gl_log_write(GL_LOG_WARN, __FILE__, __LINE__, ...)
pr_error   -> gl_log_write(GL_LOG_ERROR, ...); exit(-1);
```

活动调用点 202 个；`fops 2.c` 的 27 个不计（未编译）。

## fd 注册

`attachFd43499` 不再是 no-op：

```c
return gl_log_attach_fd(fd, debug != JNI_FALSE, GL_LOG_DEBUG);
```

`gl_log_attach_fd`：

- `fcntl(fd, F_DUPFD_CLOEXEC, 0)` 复制一份；
- 旧 owned fd 关闭；
- 设置 timestamps / min level；
- 返回 owned dup fd；
- `fd < 0` 注销并返回 -1。

Kotlin 的 `LogFile.fd` 仍由 Kotlin 拥有；native 只持有副本。

## authority

- 43499：`common/gl_log`（DSO-local fd + level/timestamps）。
- 43284/legacy：仍为 P6c 中间态 `attachFd* = -1`。
- 因此 P6c 的 `create(logFd)` 对 43499 已被本方案取代；
  P6c 若继续，只服务 43284 或未来 per-handle sink。

## 全局状态

`common/gl_log.c` 有 4 个 DSO-local 可写数据：

```text
g_export_fd
g_min_level
g_timestamps
g_start_ms
```

43499 的 `b/B` 仍为 0，但 `d` 新增 4 个（非 `.data.rel.ro`）。
这是用户要求接入标准日志库带来的明确例外；攻击/会话状态仍为零全局。

## 验证

```text
build 0 warnings
flatten S1 19/19 PASS
flatten S2 11/11 PASS
flatten S3 4/4 PASS（ignore close）
flatten S4 17/17 PASS
host gl_log test rc=0（attach dup / previous close / levels）
```

设备 logcat 实测（GHOSTLOCK_W1_ONLY=1，warm）：

```text
I GhostLockNative: main.c:443 [route] threads joined
I GhostLockNative: util.c:682 [spray] mm_struct leaked=0xffffff885faedc00
I GhostLockNative: main.c:1147 SELinux permissive
I GhostLockNative: main.c:1322 private scratch repaired; releasing quarantine
I GhostLockNative: main.c:1343 [T+7812ms] Write 1 complete
```

说明：standard logger 在 Android 只写 logcat/fd，不再写 stdout；standalone 终端会安静。

## 未做

- 未用新日志 binary 重跑完整冷机 root（只做 W1-only 设备采样）。
- `pr_error` 的 `exit(-1)` 未做独立子进程退出码测试。
- `fops 2.c` 未编译，仍是重复文件。
