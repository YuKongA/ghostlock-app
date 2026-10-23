# S8f 证据：剩余等待/轮询参数化（2026-10-21）

## 改动

`Glk43499Config` 增：

```text
retry_write_backoff_us=100000
retry_write_post_us=50000
ksu_poll_us=100000
ksu_log_wait_us=500000
enforce_wait_us=100000
race_poll_us=1000
slab_drain_us=20000
perf_sample_period=5000
```

替换：

- `retry_write_stage` 失败 backoff / 验证后等待。
- post-root KSU/enforce 轮询。
- owner/waiter/route 1000us 轮询。
- slab drain 20000us。
- `perf_find_task` 收 `sample_period`；`child_pipes.perf_sample_period`。
- kernel5 resident 200/100ms 用 `w1_settle_us` 派生。
- collision 轮询用 `w3_settle_us`。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

flatten S1 19/19 PASS
flatten S2 11/11 PASS
flatten S3 4/4 PASS（ignore close）
flatten S4 17/17 PASS

ghostlock43499 b/B = 0
ghostlock43284 b/B = 0
```

活跃源码中已无 `usleep(<数字>)`；`fops 2.c` 为未编译重复文件。

## 文档

- [S8e](s8e-w-timing-param.md)
- [P6c-1](p6c1-log-sink.md)
