# Inline 拆分批次 1（2026-10-22）

## 目标

在不改变 `cmp_disasm` 六目标入参/调用顺序的前提下，先把 `do_one_write`
与 `run_main_route_threads` 的内部阶段拆成 `always_inline` helper。

## 变更

### do_one_write

```text
do_one_write_resident()  5.x resident path
do_one_write_route()     prepare page + run_main_route_threads
do_one_write()           request + route choice + calls above
```

两个 helper 均为 `static inline __attribute__((always_inline))`。

### run_main_route_threads

```text
route_start_threads()   waiter/owner/consumer pthread_create
route_stop_threads()    consume stop + owner stop + 三个 join
run_main_route_threads() reset + start + wait/requeue + stop + result
```

helper 均为 `always_inline`。

### do_kernel5_fake_lock_route

```text
k5_build_mcast_stamp()   stamp/VLA 填充
k5_send_mcast_stamp()    setsockopt + consumer go/spin/inflight + close
```

均为 `always_inline`；外层只保留 socket 失败处理与结果归一化。

### do_tcp_fake_lock_route

```text
tcp_route_setup()       state_init + alloc + start_puncher
tcp_route_log_enter()   route diagnostics
tcp_route_run()         punch go + bounded attempts
```

均为 `always_inline`；设备仅支持 multicast，TCP 路径在 A301SO 上不执行，
本批只做静态拆分与 flatten。

### do_pselect_fake_lock_route

```text
pselect_call()  按 compact_waiter 选择 pselect/select，保留原 fd_set/timeout
```

`always_inline`；外层 setup/wait/finish 不变。

### do_pselect_fake_lock_route

```text
pselect_call()  按 compact_waiter 选择 pselect/select，保留原 fd_set/timeout
```

`always_inline`；外层 setup/wait/finish 不变。

### waiter_thread / owner_thread / consumer_thread

```text
waiter_enter_chain / waiter_wait_chain / waiter_dispatch_route / waiter_finish_route
owner_lock_target / owner_wait_waiter / owner_finish
consumer_apply_nice
```

全部 `always_inline`，放在原 cmp 目标前。

## 验证

```text
CMake build 0 warnings
important_flatten do_one_write: 7/7 PASS
important_flatten run_main_route_threads: 7/7 PASS
43499 b/B = 0
```

设备：

```text
W1-only: PASS
  SELinux permissive
  W1b scratch repaired
  Write 1 complete

full thread-inline: PASS（第 3 次）
  child uid = 0
  child is root!
  exploit complete
  enforce=1 (enforcing)
  KernelSU ready

full thread-inline 前两次：W1/W2 期间设备 reboot（概率性）

full k5-inline：一次 PASS
  child uid = 0 / child is root!
  exploit complete
  enforce=1 (enforcing)
  KernelSU ready

full tcp-inline（同一 multicast 路径）：
  W1-only PASS
  full：一次 W2 15 轮失败；多次 reboot
  仅证明 TCP 拆分静态/flatten 通过；TCP 路径在 A301SO 不执行

5.10 设备（mondrian，5.10.252-gki-gabcbe4b2bc10）：
  已连接，可 adb root；历史 gate 记录 select 路线可成功
  固定 5.10 probe + consumer：
    step=0 errno=0 calls=1 success=1
    pselect_call 拆分后 select 路线 PASS
  pselect_call 非 inline：5.10 select PASS
```

设备检测方式：不再组合 adb+logcat 后台；
先 `adb shell 'timeout 120 ... </dev/null >/dev/null 2>&1'`，再单独 `adb logcat -d`。

logcat: `/tmp/route-full-logcat.log`（主机临时文件，未入库）。

## 注意

- `cmp_disasm` 的逐指令对比在本分支上仍有既有差异，不能作为唯一门禁；
  本批用 `important_flatten` 保证调用数/参数/顺序。
- helper 仍依赖 `s->config`/`s->target`；后续需继续把参数外提。
- 非 inline 版本尚未测试。
