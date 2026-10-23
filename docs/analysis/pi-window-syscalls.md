# PI 窗口 syscall 约束（2026-10-22）

规则：PI 窗口内不能新增原本没有的 syscall；日志写 logcat/fd 也是 syscall。

## 已移除

```text
waiter_dispatch_route: mcast ghost disarm 的 pr_info
consumer_thread: consumer thread running 的 pr_debug
```

两处都位于 waiter/owner/consumer 并发窗口附近，移除后 PI 窗口内不再有
`gl_log_write` / logcat write。

## 当前 PI 窗口保留

```text
futex_op(FUTEX_WAIT_REQUEUE_PI)
do_kernel5_fake_lock_route 的 socket/setsockopt/close
futex_op(FUTEX_LOCK_PI unmask)
route_done
futex_op(FUTEX_UNLOCK_PI)
```

这些是原有攻击原语，不新增日志/文件/时间 syscall。

## 验证

```text
W1-only: PASS
full multicast: 一次 PASS 到 KernelSU ready
随后重复一次 full: reboot（概率性，未取得日志）
```

因此约束已落实，但 A301 完整链重复性仍未稳定。
