# S8b 设计：Layout 替换 target.h 宏（2026-10-21）

> 从属 [S8a](s8a-layout-defaults-design.md)。目标：运行时只读 `target.layout`；
> target.h/common.h 常量仅作 `glk43499_layout_defaults` 的默认值。

## 范围

仅 43499；43284 已无固定值全局，暂不动。

## 替换规则

1. `KIMAGE_TEXT_BASE` → `L->kimage_text_base`
2. `P0_PAGE_OFFSET/P0_PHYS_OFFSET/P0_KERNEL_PHYS_LOAD` → layout
3. `DIRECT_MAP_END`、`KERNELSNITCH_IDENTITY_*` → layout
4. payload：
   `LOCK_OFF/W0_OFF/FOPS_OFF/RIGHT_OFF/LEFT_OFF/FAKE_TASK_OFF/CRED_COPY_OFF`
   `TCP_FAKE_TASK_OFF/TCP_CRED_COPY_OFF/SKB_DATA_DELTA/SKB_FRAG_BIAS` → layout
5. fake waiter/task：
   `FAKE_WAITER_*` / `FAKE_TASK_*_OFF` / `FAKE_TASK_PRIO` / `FAKE_WAITER_PRIO`
   → layout
6. `runtime_struct_offsets.h` 的 `_RSO_IMAGE`：
   `(L->kimage_text_base + _RSO_64(field,fallback))`
   每个使用 `INIT_TASK/INIT_CRED/SLIDE_*/SELINUX_*` 的函数首行加：
   `const Glk43499Layout *L = &s->target.layout;`

## publish 例外

`publish_active_offsets` 不能用替换后的 `INIT_CRED` 宏（会变成用 layout 写 layout）：

```c
/* defaults 已设置 layout.init_cred_image = kimage + 静态 INIT_CRED_OFF */
if (t->off_init_cred) {
  target->layout.init_cred_image =
      target->layout.kimage_text_base + t->off_init_cred;
}
if (t->kernel_phys_load) {
  target->layout.p0_kernel_phys_load = t->kernel_phys_load;
} else if (soc == SOC_MTK) {
  target->layout.p0_kernel_phys_load =
      target->layout.kimage_text_base - target->layout.mtk_vaddr_base;
} else if (soc == SOC_XRING) {
  target->layout.p0_kernel_phys_load = target->layout.xring_kernel_phys_load;
} else if (strncmp(t->uname_r, "6.12.", 5) == 0) {
  target->layout.p0_kernel_phys_load = target->layout.qc_gki_6_12_phys_load;
}
```

- 不使用动态重定义后的 `INIT_CRED_OFF` fallback；off=0 时保留 defaults。
- 不得使用 `INIT_CRED` / `KIMAGE_TEXT_BASE` 宏。
- layout 增 `mtk_vaddr_base`、`xring_kernel_phys_load`、`qc_gki_6_12_phys_load`，
  defaults 从 target.h 填；publish 只读 layout。

## 局部约定

- 函数已有 `s`：加 `L`。
- `p0_data_alias/data_addr` 已有 target：用 `&target->layout`。
- `payload_geometry` 已有 s：`L`。
- `prepare_kernel_page` 的 `leaked` 校验用 layout identity/direct map。
- `do_tcp/do_pselect` 的 `pselect_waiter_shift` 仍读 offsets，不涉及 layout。

## 不改

- `target.h/common.h` 的常量值。
- offsets.json 覆盖字段。
- pselect fd_set word shift（offsets）。
- 43284。

## 风险

- `_RSO_IMAGE` 新增 `L` 后，遗漏函数的编译错误会直接暴露；逐批修。
- `data_addr` 的 p0 必须来自 layout；S8a 已保证复制。
- 宏替换不能改变 important call/syscall 顺序；只改常量来源。
- `FAKE_TASK_PRIO_OFF` 运行期固定取 RSO fallback **0x94**；target.h 的 0x84 不再作运行期来源。
  `glk43499_layout_defaults` 加 `assert(l.fake_task_prio_off == 0x94)`（debug）。

## 批次

S8b-1：image/p0/direct map/identity（`data_addr`、publish、leak 校验）。

`_RSO_IMAGE` 的 L 传播清单：

| 文件 | 函数 | 已有 s/target |
|---|---|---|
| main.c | publish_active_offsets | target |
| main.c | run_exploit | s |
| main.c | run_main_route_threads | s |
| fops.c | do_tcp_fake_lock_route | s |
| util.c | log_startup_context | s |
| util.c | prepare_skb_payload | s |
| util.c | prepare_kernel_page | s |
| main.c | perf_find_task | 无 s；改为收 `uintptr_t direct_map_end` |
| main.c | child_init | p；调用 perf_find_task(p->direct_map_end) |

`common.h` 的 `SLIDE_*` 宏只调用 `data_addr(&s->target, ...)`，不需要 L；
其 `*_IMAGE` 参数在调用点展开，仍需要上述函数的 L。

逐函数加 `const Glk43499Layout *L = &s->target.layout;`；publish 用 `&target->layout`。
`perf_find_task` 无 session，改为参数 `uintptr_t direct_map_end`；
`child_pipes` 增 `uintptr_t direct_map_end`，由 run_exploit 从 layout 填入，
`child_init` 传 `p->direct_map_end`。
S8b-2：payload geometry/lock/w0/task/fops/cred。
S8b-3：fake waiter/task offsets。
每批 build + flatten S1/S2/S4 + b/B。

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
  --parent prepare_kernel_page --helper prepare_reset \
  --helper prepare_spray_contexts --helper prepare_leak_children \
  --helper prepare_wait_collisions --helper prepare_leak_mm \
  --helper prepare_payload --helper prepare_fail

# 每批记录 base/cur、COUNT/ORDER、args_diff。
llvm-nm b/B=0
```
