# S8c 证据：传输/喷溅/pselect 几何参数化（2026-10-21）

> 设计：[s8c-transport-geometry-design](s8c-transport-geometry-design.md)。
> 实现调整：这些字段原本已在 `Glk43499Config`；本批只补 `skb_reclaim_sends`，
> 不重复放进 layout。`Glk43499Layout` 仍只保留 `page_size`。

## Config

`glk43499_config_defaults` 填：

```text
select_nfds=320
select_timeout_us=200000
select_enter_delay_us=50000
tcp_attempts=2000
tcp_arm_seq=16
tcp_post_hold=20000
tcp_punch_len=16 MiB
mm_order=3
mm_partials=5
skb_reclaim_sends=4
```

派生：

```c
order3_size = layout.page_size << config.mm_order
skb_send_size = order3_size * 2
```

## 替换

### util

- `prepare_ctxs`：config.mm_partials。
- `prepare_reset`：order3/skb 派生；skb_buf 唯一 malloc；assert 几何。
- `prepare_payload`：skb_send、config.skb_reclaim_sends。
- `prepare_kernel_page`：base mask 用 order3。
- `prepare_skb_payload`：memset/chunk 用 skb/order3。
- `fill_profile_cred_copy`：cred_copy_size 上限用 order3 参数。

### fops

- TCP：config.tcp_attempts/tcp_arm_seq/tcp_post_hold/tcp_punch_len。
- pselect：config.select_nfds/timeout/enter_delay。
- 无 s helper 收 `int nfds`：
  `pselect_words_per_set`、`open_selected_fds`、`reserve_standard_io`、
  `pselect_put_waiter_word`。

### main

- `reserve_standard_io(..., config.select_nfds)`。
- 主 route delay 用 config.select_enter_delay_us。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

flatten S1：base=19 cur=19 args_diff=16 PASS
flatten S2：base=11 cur=11 args_diff=10 PASS
flatten S3：base=4 cur=4 args_diff=4 PASS（ignore close）
flatten S4：base=17 cur=17 args_diff=13 PASS

llvm-nm 43499 b/B = 0
```

## 剩余

- fast_repair route 的 5000us 仍是局部常量。
- common.h 的旧宏保留作默认说明；运行期已不展开。
- `fops 2.c` 为未编译的重复文件，未触碰。

## 文档

- [S8b-2/3](s8b2-payload-param-split.md)
- [S8c 设计](s8c-transport-geometry-design.md)
- 日志：`build/analysis/flatten-s8c-*.log`
