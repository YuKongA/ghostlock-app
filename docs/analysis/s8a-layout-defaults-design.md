# S8a 设计：Layout 默认值与 target 复制（2026-10-21）

> 发现：`select_offsets` 写入 `target->layout.p0/init_cred` 后，`glk_session_create_43499` 的
> `glk43499_target_init` 会 `memset` target 且不复制 layout，运行态 lost p0/init。
> 本批先修默认值/复制，不替换调用点宏。

## 改动

1. `Glk43499Layout` 增字段：
   `fake_waiter_prio`、`fake_task_prio`、`fake_task_uclamp_req_off`、`fake_task_uclamp_off`。
2. `glk43499_layout_defaults(Glk43499Layout *)`：
   从 target.h/common.h 常量填满：
   `kimage_text_base/p0_page_offset/p0_phys_offset/direct_map_*/vmemmap_start/`
   `kernelsnitch_identity_*/p0_kernel_phys_load/init_cred_image/page_shift/page_size/`
   `lock_off/w0_off/fops_off/right_off/left_off/fake_task_off/cred_copy_off/`
   `tcp_*/skb_*/fake_waiter_*/fake_task_*/`
   `fake_waiter_prio/fake_task_prio/fake_task_uclamp_*`。
3. `glk43499_target_init`：memset 后调用 defaults，再 copy offsets/release。
4. `glk_session_create_43499`：target_init 后
   `inner->target.layout = target->layout;`
   保留 `select_offsets/publish` 写入的 p0/init。
5. 只改 `glk43499_handle_create_default`：
   `memset(&target)` 后、`select_offsets` 前调用 `glk43499_layout_defaults(&target.layout)`。
   `run_with_session` 只包装 handle，不直接持有 target，不改。
6. `publish_active_offsets` 继续只覆盖 p0/init，不覆盖其他 layout。

## 所有权

- layout 是 target 值；create 后复制到 inner target。
- 无新全局；`Glk43499Layout` 仍是 POD。

## 验证

```text
build 0 warnings
llvm-nm 43499 b/B=0
```

S1/S2/S3/S4/S7 的 flatten 命令已分别在 [split-large-functions](../plan/split-large-functions.md)
各节列出；本批复跑同一组 parent/helper 与 baseline/candidate 路径，记录
`base/cur`、`COUNT/ORDER`、`args_diff` 到 S8a evidence。不新增或修改命令。

新增直接检查（不依赖真机）：

```c
assert(target->layout.lock_off == LOCK_OFF);
assert(target->layout.kimage_text_base == KIMAGE_TEXT_BASE);
```

可放在 `glk43499_target_valid` 的 debug 断言或 create 后日志；不改变 release 行为。

## 风险

- defaults 必须在 select 前，否则 publish 的 p0/init 被 memset。
- create 的 layout 复制必须在 target_init 之后；不能反过来。
- 不改变 target.h 常量值；只把它们搬到 layout。
