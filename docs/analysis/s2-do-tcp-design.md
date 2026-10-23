# S2 设计：do_tcp_fake_lock_route 拆分（2026-10-21）

> 从属 [split-large-functions](../plan/split-large-functions.md) S2。目标：170 行/20 分支 → 小函数；不改 socket/线程/cleanup 顺序。

## 状态

```c
struct tcp_route_state {
  int client_fd, server_fd, punch_fd;
  char *map;
  size_t page_size;
  pthread_t puncher;
  int puncher_started;
  int route_ok;
  struct tcp_punch_state punch;  /* thread arg must outlive pthread_create */
};

static void tcp_route_state_init(struct tcp_route_state *st) {
  st->client_fd = st->server_fd = st->punch_fd = -1;
  st->map = MAP_FAILED;
  st->page_size = 0;
  st->puncher_started = 0;
  st->route_ok = 0;
}
```

状态只存在于 `do_tcp_fake_lock_route` 栈；线程 arg 指向 `st.punch`，join 前不得离开作用域。

## 拆分

```c
static int tcp_route_alloc(Glk43499Session *s, struct tcp_route_state *st);
static int tcp_route_start_puncher(Glk43499Session *s, struct tcp_route_state *st);
static enum tcp_attempt tcp_route_attempt(Glk43499Session *s,
                                          struct tcp_route_state *st, int i);
static void tcp_route_cleanup(Glk43499Session *s, struct tcp_route_state *st);
```

### tcp_route_alloc

0. `long ps = sysconf(_SC_PAGESIZE)`；`if (ps <= 0)` → step **47**、
   `route_last_errno=EINVAL`，返回 -1（覆盖 0 与 -1），再把 `ps` 转 `size_t`；
   在填 map 之前完成。cleanup 看到 step=47 不得改写成 45。
1. `tcp_make_pair` → client/server；**同批修 `tcp_make_pair`**：每个 close 后把
   `*client_fd`/`*server_fd` 置 -1，避免 cleanup double-close。
2. `memfd_create` + `fallocate(TCP_PUNCH_SHMEM_LEN)`
3. `mmap` + 按 `st->page_size` 填 0x55

失败：设置 `route_last_step` 41/42/43 + errno，返回 -1；已建 fd 由 cleanup 统一关。

### tcp_route_start_puncher

- 只读取 `st->page_size`（alloc 已设置），不重新 sysconf
- 初始化 `st->punch = { .s=s, .fd=punch_fd, .page_size=st->page_size }`
- reset：`tcp_punch_stop/phase/failed`、`punch_consume_*`、`consumer_calls/success`
- `pthread_create(&st->puncher, ..., tcp_punch_thread, &st->punch)`
- 失败：step 44 + errno，return -1；成功 `puncher_started=1`。

### tcp_route_attempt

输入 `i`，返回：

```text
TCP_ATTEMPT_SUCCESS  消费者 calls/success 增加
TCP_ATTEMPT_RETRY    phase 未命中或 calls/success 未增
TCP_ATTEMPT_FAIL     puncher_failed
```

步骤保持：

1. 读 calls_before/success_before
2. `send(server_fd, 0x33*64, MSG_DONTWAIT)`
3. 等 `tcp_punch_phase` 清零 → spin 等到再次置位/失败/10M
4. failed → step46 + errno + log + FAIL
5. 构造 zc：0x18=map+page, 0x20=64, 0x28=SLIDE_INIT_TASK, 0x30=fake_lock
6. `getsockopt(TCP_ZEROCOPY_RECEIVE)`
7. `i >= TCP_ARM_SEQ && ret==0` → `punch_consume_go=i` → post_hold spin → `tcp_wait_for_consumer_idle`
8. calls/success 未增 → 每 100 次或 ret!=0 打日志 → RETRY
9. 增加 → route_ok=1，step/errno=0 → SUCCESS

### tcp_route_cleanup

顺序不变：

```text
punch_consume_go=0; punch_consume_stop=1
tcp_punch_go=0; tcp_punch_stop=1
if puncher_started: pthread_join
if map != MAP_FAILED: munmap
close punch_fd/server_fd/client_fd（>=0）
```

最后：

```text
if (!route_ok && route_last_step==0) route_last_step=45
pr_info("tcp route done=...")
```

## do_tcp 编排

```text
validate page/fake_lock/fake_fops
tcp_route_state_init(&st)
tcp_route_alloc
tcp_route_start_puncher
log enter
tcp_punch_go=1; main_route_delay=0
for i=1..TCP_ROUTE_ATTEMPTS:
  at = attempt(i)
  if SUCCESS break
  if FAIL break
tcp_route_cleanup
```

## 所有权

| 对象 | owner | 终结 |
|---|---|---|
| client/server fd | `tcp_route_state` | cleanup close |
| punch_fd | state | cleanup close |
| map | state | cleanup munmap |
| puncher thread | state | cleanup join |
| punch arg | `st.punch` | join 前有效 |

## 风险

- `punch` 不能在 helper 栈上；必须放 state。
- 失败路径不能提前 return 绕过 cleanup；do_tcp 单一出口。
- `route_last_step` 只在原位置写 40/41/42/43/44/45/46；新增 47=page_size 获取失败。
- cleanup 的 45 只在 `route_last_step==0` 时写；47 保持。
- 补 page_size 失败路径测试：同时覆盖 `sysconf` 返回 0 与 -1。
- `post_hold`/arm_seq 常量不变。
- `tcp_route_attempt` 内 `saved_errno` 必须在 getsockopt 后立即读。

## 门禁

```text
build 0 warnings
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent do_tcp_fake_lock_route \
  --helper tcp_route_alloc --helper tcp_route_start_puncher \
  --helper tcp_route_attempt --helper tcp_route_cleanup
# FLAT-COUNT/ORDER PASS；args_diff 仅报告
cmp_disasm --important-only：无 COUNT/ORDER FAIL
llvm-nm b/B=0
```

`args_diff` 仅报告；close/futex/send 的 frame 来源变化人工归因。
