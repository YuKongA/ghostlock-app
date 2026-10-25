# P6c 设计 v5：JNI 日志 sink 与零全局约束（2026-10-21）

> v1 评审：阻断 4（P6c-01..04），重要 3。本版按 A′ 修订：显式 sink、handle 私有 fd、create 传 fd。
> v4→v5：sigtimedwait 改零超时 {0,0}；EAGAIN/EINTR 直接返回；统一恢复 mask。
> 当前评审：技术阻断 0；仅 P6c-14 用户接受 pre-session 非文件日志待定。
>
> **2026-10-22 状态变更**：43499 已改接 `common/gl_log`，`attachFd43499`
> 使用 dup-owned fd；本设计的 `create(logFd)` 对 43499 被取代。
> P6c 只保留给 43284/legacy 或未来 per-handle sink。

## 问题

Kotlin `NativeLogBridge.attach(fd)` 在 Application 启动时调用，早于任何 create；
当前 `attachFd43499/43284` 只有 `(fd,debug)`，无法写入“未来 handle”。
43499 `pr_info` 与 43284 `DF_LOG*` 都是 varargs，现有 `GlkLogSink.write` 只收 message。

## 约束

1. 43499/43284 `llvm-nm b/B` 保持 0。
2. 不新增进程级 fd 全局。
3. fd 所有权明确；Kotlin 关闭 fd 后 native 不得再写。
4. 日志不改变重要 call/syscall 序列；PI 窗口内不调用 sink。

## 结构（A′）

### 公共回调形状不变

`GlkLogSink { void (*write)(void *user, int level, const char *message); void *user; }`
只作为**借用**放进 session/state，不放进 Config。

### handle 私有 fd 实现

```c
typedef enum { GLK_FD_FILE, GLK_FD_SOCKET, GLK_FD_FIFO } GlkFdKind;
typedef struct {
  int fd;          /* dup 后的 fd，handle 拥有 */
  GlkFdKind kind;  /* create 时 fstat/getsockopt 一次判定 */
  atomic_bool disabled;  /* writer 失败后只置位，不 close */
  atomic_bool in_pi;     /* PI 窗口内丢弃 */
} GlkFdSink;
```

- 43499：`Glk43499Handle` 内含 `GlkFdSink`；create 时 `dup(log_fd)`，把
  `inner.log = {fd_sink_write, &handle->fd_sink}`。
- 43284：`Glk43284State` 增 `GlkLogSink log`；`Glk43284Handle` 内含 `GlkFdSink`；
  create 时同样 dup/绑定。
- create/失败 create/destroy 用 `fcntl(log_fd, F_DUPFD_CLOEXEC, 0)`，副本 close-on-exec；
  fork/exec 不继承。
- destroy/失败 create：关闭 dup fd；不关闭 Kotlin 原 fd。
- writer 永不 close；写失败只 `atomic_store(disabled,true)`，后续线程丢弃。
- handle 增 `atomic_bool running`：run 进入 CAS false→true，返回前清 false；
  destroy 若 running=true 返回具名 busy，不置 disabled、不 close。
- destroy 只在 running=false 时置 disabled，再 close 一次；
  run 返回时攻击线程已 join（43499 `pthread_join`，43284 waitpid）。
- JNI `destroy*` 返回 `jint`（0=destroyed，1=busy）；Kotlin 必须检查。

### 格式化桥

```c
void glk_logf(GlkLogSink *sink, int level, const char *fmt, ...);
```

- `vsnprintf` 到固定 512 B 栈缓冲；超长截断并保留 `...`。
- 每次一行、单次写：
  - SOCKET：`send(..., MSG_NOSIGNAL)`。
  - FIFO/PIPE：`pthread_sigmask(SIG_BLOCK, SIGPIPE)` → `write` →
    若 EPIPE 用 `sigtimedwait` **零超时 `{0,0}`** 消费 pending SIGPIPE；
    `EAGAIN`/`EINTR` 直接返回，不等待。无论成功/失败/提前返回，
    在同一条清理路径恢复 `pthread_sigmask`。
    测试必须覆盖“SIGPIPE 已被忽略 + pipe reader 已关闭”，断言不阻塞。
  - FILE：`write`。
- 部分写/EPIPE 只置 `disabled`，绝不 close；fifo 路径不得把 SIGPIPE 留到恢复后。
- 不实现 level/timestamps 字段；等级过滤由调用点决定。
- 无 malloc、无锁、无 JNI、无回调进 Kotlin。
- `glk_logf` 先查 `disabled`/`in_pi`；sink 为空时 no-op。
- `glk_log_pi_enter/exit(GlkLogSink*)` 设置 `in_pi`；PI 内所有日志丢弃。
  43499 PI 日志清单（均在 futex syscall 返回后或进入前，无窗口内日志）：

| file:line | 函数 | 处理 |
|---|---|---|
| fops.c:112 | kernel5_resident_start | 进入前，保留 |
| fops.c:123 | kernel5_resident_write | mr_adjust 返回后，保留 |
| main.c:283 | waiter_thread | futex 返回后错误路径，保留 |
| main.c:310 | waiter_thread | futex 返回后，保留 |
| main.c:327 | owner_thread | futex 返回后错误路径，保留 |
| main.c:343 | consumer_thread | 进入循环前，保留 |
| main.c:412/418/423/426/436 | run_main_route_threads | 创建/等待/join 边界，保留 |

  `mr_x`/`mr_y`/`mr_stamp` 无日志；`in_pi` 仍作为防御位，进入 futex 前后设置。
  43284 无 futex PI，`in_pi` 恒 false。

## JNI / Kotlin 语义变更

```text
create43499(home: String?, logFd: Int): Long
create43284(encap, sender, spi, key, logFd: Int): Long
```

- `logFd < 0`：不安装 sink。
- `attachFd43499/43284/Legacy` 保留为**兼容 no-op**，不再声称注册未来 handle；
  Kotlin `attachFd*` 返回 -1 时必须显示 `=no-op`，不能显示 `=ok`；
  只有 create(fd) 实际写入后才能显示 connected。
- Kotlin `NativeLogBridge.attach` 只负责加载库并返回状态；fd 由 create 传入。
- P6c 完成判据：create(fd) 后写入一行、destroy 后 fd 关闭、无泄漏；不能只看 attach 返回。

## fd 生命周期

1. Kotlin `LogFile` 拥有原 fd；传入 create 后仍可关闭。
2. native create `dup(fd)`，只拥有副本。
3. create 失败：关闭副本，返回 0。
4. destroy：关闭副本；幂等。
5. attach no-op 不保存 fd。

## 线程/顺序

- sink 由 C fd writer 实现；多线程允许行间交错，单行不交错。
- 43499 线程入口已经收 `Glk43499Session*`，日志宏改为 `glk_logf(&s->log,...)`。
- 43284 函数已经收 `Glk43284State*`，`DF_LOG*` 改为 `glk_logf(&st->log,...)`。
- PI 窗口内日志改为延迟/丢弃；不改攻击 call 序列。

## 分批

1. P6c-1：新增 `GlkFdSink`/`glk_logf`，不改调用点；单测格式化/截断/EPIPE。
2. P6c-2：43499 线程入口 + route 日志接入；每批 important_trace 验计数/顺序。
3. P6c-3：43284 日志接入；同样验 important PASS。
4. P6c-4：JNI create 增 `logFd`；Kotlin 桥传 fd；真机写/关闭门禁。

## 遗留风险

- 160+ 日志调用点需要逐批改；未改完前 attach 仍 no-op。
- pre-session 日志完成表（降级目标，不是静默丢）：

| 点 | 目标 |
|---|---|
| main.c init_cpu_config / init_runtime_paths / write_root_script | logcat（现有 pr_*） |
| main.c select_offsets | logcat |
| offsets_json.c:309 的 `fprintf(stderr)` | stderr |

  这些发生在 handle 创建前，不能拿到 sink；用户若要求统一文件日志，需给这些 helper
  传临时 `GlkLogSink`。P6c 完成表必须单列“非文件日志”并取得用户明确接受；
  未接受前 P6c 不得标完成。
- 43499 的 `pr_info` 有些在无 session 的 helper 中；需补 sink 参数或移到 session 调用点。
- 43284 的 `DF_LOG*` 宏当前无 st；需宏改造，不能引入全局。

## 证据

P6b 状态：[p6b-jni-session.md](p6b-jni-session.md)。
