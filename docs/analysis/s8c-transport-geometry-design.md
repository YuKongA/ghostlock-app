# S8c 设计：传输/喷溅/pselect 几何参数化（2026-10-21）

> 从属 [S8b](s8b-layout-replace-design.md)。目标：把剩余传输几何从宏搬进 layout；
> target.h/common.h 仅作 defaults。
>
> **实现调整（2026-10-21）**：`Glk43499Config` 已存在 select/tcp/mm 字段；
> 本批使用 Config（补 `skb_reclaim_sends`），不重复放进 layout。layout 只保留 page_size。

## 新 layout 字段

```c
uint32_t mm_order;                 /* 3 */
uint32_t mm_partials;              /* 5 */
uint32_t skb_reclaim_sends;        /* 4 */
uint32_t tcp_punch_shmem_len;      /* 16 MiB */
uint32_t tcp_route_attempts;       /* 2000 */
uint32_t tcp_arm_seq;              /* 16 */
uint32_t tcp_post_getsockopt_hold; /* 20000 */
uint32_t pselect_route_nfds;       /* 320 */
uint32_t pselect_timeout_sec;      /* 0 */
uint32_t pselect_timeout_usec;     /* 200000 */
```

派生：

```c
order3_size = layout.page_size << layout.mm_order
skb_send_size = order3_size * 2
```

## 替换

### util

- `prepare_ctxs`：`mm_partials`、`mm_objs_per_slab`。
- `prepare_reset`：`skb_buf = malloc(skb_send_size)`（唯一 owner），
  `order3_size / mm_struct_sz()`；assert `skb_send_size == order3_size * 2`。
- `prepare_payload`：`skb_send_size`、`skb_reclaim_sends`。
- `prepare_kernel_page`：base mask `~(order3_size-1)`。
- `payload_geometry`：`order3_size` 步长由调用方传/读 layout。
- `prepare_skb_payload`：只 `memset(s->rt.skb_buf, 0, skb_send_size)` 和 chunk 循环，
  不再 malloc；chunk 步长统一 `order3_size`。

### fops

- `tcp_route_alloc/start/attempt/do_tcp`：`tcp_*` 全读 layout。
- `pselect_route_setup/wait`：`pselect_*` 全读 layout。
- 无 s 的 helper 收 `uint32_t nfds`：
  - `pselect_words_per_set(int nfds)`
  - `open_selected_fds(..., int nfds)`
  - `reserve_standard_io(int backup_fd[3], int nfds)`
  - `pselect_put_waiter_word(..., int nfds)`

### main

- `exploit_prelude` 调用 `reserve_standard_io(..., layout.pselect_route_nfds)`。
- `do_pselect` 的 setup 同样调用 `reserve_standard_io(..., L->pselect_route_nfds)`。
- 两个调用点都必须改；不能保留无参宏。
- 其余 TCP/pselect 经 fops 参数。

## 不变

- 所有默认值等于当前宏值。
- 32 位/64 位宽度：layout 字段 uint32_t，派生 size_t。
- 不改变 important call/syscall 顺序。

## 风险

- `ORDER3_SIZE` 用 `PAGE_SIZE` 宏；layout.page_size 必须 4096，defaults 已设。
- `reserve_standard_io` 的 F_DUPFD base 需要在调用方传 nfds；漏传会导致 fd 冲突。
- `pselect` fd_set 字数必须由 nfds 计算，不能再用宏。
- `SKB_SEND_SIZE` 派生必须与 `skb_buf` 分配一致。

## 门禁

```text
build 0 warnings
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent run_exploit --helper exploit_prelude --helper run_phase1_probe \
  --helper run_w1_stage --helper run_vr_tag_clear --helper run_w3_attempts \
  --helper run_w2_w3_chain --helper run_post_root

python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent do_tcp_fake_lock_route --helper tcp_route_alloc \
  --helper tcp_route_start_puncher --helper tcp_route_attempt --helper tcp_route_cleanup

python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent do_pselect_fake_lock_route --helper pselect_route_setup \
  --helper pselect_route_wait --helper pselect_route_finish \
  --helper pselect_route_setup_cleanup --ignore-symbol close

python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent prepare_kernel_page --helper prepare_reset \
  --helper prepare_spray_contexts --helper prepare_leak_children \
  --helper prepare_wait_collisions --helper prepare_leak_mm \
  --helper prepare_payload --helper prepare_fail

# 每批记录 base/cur、COUNT/ORDER、args_diff。
llvm-nm b/B=0
```
