# 5.10 select probe（2026-10-22）

设备：

```text
b4541958 / 23013RK75C / mondrian
5.10.252-gki-gabcbe4b2bc10
adb root
```

## 临时 profile

来源：设备 `/sdcard/Download/k60-mondrian-20260531-full.conf`。
`temp_select_510.c` 内固定：

```text
kernel_major=5
mcast_waiter_off=0（强制不走 multicast）
compact_waiter=0/1 试验
task/cred/off 来自该 conf
```

## 结果

### 仅 do_pselect（无 consumer）

```text
prepare_kernel_page ok
pselect setup shift=0 page=...
pselect ret=0 errno=0 calls=0 success=0
step=33
```

原因：probe 未启动 consumer；pselect 超时返回。

### 只启动 consumer + do_pselect

```text
prepare_kernel_page ok
pselect setup shift=0
consumer thread running on cpu=1
pselect post-select compact=0 ret=0
pselect returned calls=1 success=1
pselect route done step=0 errno=0
select probe done step=0 calls=1 success=1
```

结论：5.10 的 select 路线在拆分后的 `pselect_call` 上成功。

### 非 inline 版本

把 `pselect_call` 从 `always_inline` 改为普通 `static` 后：

```text
build 0 warnings
important_flatten do_pselect_fake_lock_route 8/8 PASS
5.10 select probe: step=0 calls=1 success=1
```

非 inline 版本保留，作为第一个通过真机 select 验证的非 inline helper。

随后把 `prepare_page_align` 从 `prepare_kernel_page` 拆成普通 static 函数；
5.10 select probe 仍为：

```text
prepare_kernel_page ok
pselect returned calls=1 success=1
step=0 errno=0
```

因此 `prepare_page_align` 非 inline 也通过 5.10 select 验证。

同样把 `consumer_apply_nice` 从 `always_inline` 改为普通 static 后，
5.10 select probe 仍为：

```text
consumer thread running on cpu=1
calls=1 success=1
step=0 errno=0
```

说明 cmp 目标 `consumer_thread` 的非 inline helper 也通过真机 select 验证。

继续把 payload 写入拆成普通 static：

```text
payload_write_task_links()
payload_write_task_cred()
```

5.10 select probe 仍为 `calls=1 success=1 step=0`，说明 page/payload 非 inline 拆分通过。

继续把 spray 初始化拆成普通 static：

```text
prepare_spawn_spray()
prepare_start_kernelsnitch()
```

5.10 select probe 仍为 `calls=1 success=1 step=0`，page/scan 初始化拆分通过。

再拆出 `prepare_alloc_skb`（普通 static），5.10 select 仍为
`calls=1 success=1 step=0`，SKB 分配/ctx 初始化拆分通过。

继续拆 leak children 生命周期：

```text
prepare_spawn_leak()
prepare_open_leak_fds()
prepare_kill_leak_children()
```

5.10 select 仍为 `calls=1 success=1 step=0`，通过。

继续拆 collision 等待：

```text
prepare_scan_beat()
prepare_wait_leak_child()
```

5.10 select 仍为 `calls=1 success=1 step=0`，通过。

### 早期 run_main_route_threads 试验

```text
GHOSTLOCK_TCP_ROUTE=0
consumer/owner/waiter 一起启动时设备 reboot，未取得日志
```

后续改为只启动 consumer，route 成功；问题在早期 probe 的线程组合，不是 `pselect_call`。

## 结论

- `pselect_call` 拆分本身静态/flatten 通过；
- 5.10 的 page/route 组合尚未验证成功；
- 需要先修正 probe（consumer/target/page/compact 参数），再判断拆分。

## 下一步

1. 用已知 gate 的 `NativeSelectStackInstrumentedTest` 参数对齐 probe。
2. 先只启动 consumer，再单独调用 route，分离 page 与线程问题。
3. 每次 5.10 重启后等 20 秒。
