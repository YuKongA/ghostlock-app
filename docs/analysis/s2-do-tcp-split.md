# S2 证据：do_tcp_fake_lock_route 拆分（2026-10-21）

> 设计：[s2-do-tcp-design](../analysis/s2-do-tcp-design.md)；评审阻断 0 后实施。

## 拆分

```text
tcp_route_state_init
tcp_route_alloc
tcp_route_start_puncher
tcp_route_attempt
tcp_route_cleanup
```

`do_tcp_fake_lock_route` 只做：

```text
validate → init → alloc → start → attempt loop → cleanup
```

## 关键修正

- `tcp_route_state` 初始化 fd=-1、map=MAP_FAILED、started/route_ok=0。
- `tcp_make_pair` 每次 close 后把 client/server 置 -1，避免 cleanup double-close。
- `page_size` 在 alloc 入口 `long ps=sysconf`；`ps<=0` → step47/EINVAL。
- `punch` 状态放在 `tcp_route_state`，线程 arg 在 join 前有效。
- do_tcp 只有**一个** `tcp_route_cleanup` 调用点；alloc/start 失败也走同一出口。
- cleanup 的 step45 只在 `route_last_step==0` 时写，47 保持。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

important_flatten：
  FLAT-SUMMARY base=11 cur=11 args_diff=10
  FLAT-RESULT: PASS

args diff：syscall/fallocate/mmap/pthread_create/getsockopt/
pthread_join/munmap/close x0；由 helper 栈帧或 state 地址变化引起。

cmp_disasm --important-only：
  functions=62 base=220 cur=218
  IMPORTANT-REMOVED do_tcp_fake_lock_route（11）为预期搬迁；
  flatten 已覆盖；其余无 COUNT/ORDER FAIL。

llvm-nm 43499 b/B = 0
```

## 文档

- [S1](s1-run-exploit-split.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/flatten-s2.log`、`build/analysis/important-s2.log`
