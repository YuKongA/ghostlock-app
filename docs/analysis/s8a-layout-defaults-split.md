# S8a 证据：Layout 默认值与 target 复制（2026-10-21）

> 修复：`select_offsets` 写入的 `target.layout.p0/init_cred` 在
> `glk_session_create_43499` 的 `target_init` 后被清零，运行态丢失。

## 改动

- `Glk43499Layout` 增：
  `fake_waiter_prio`、`fake_task_prio`、`fake_task_uclamp_req_off`、
  `fake_task_uclamp_off`。
- 新增 `glk43499_layout_defaults(Glk43499Layout *)`：
  用 target.h/common.h 常量填满 layout。
- `glk43499_target_init`：`memset → defaults → offsets/release`。
- `glk_session_create_43499`：target_init 后
  `inner->target.layout = target->layout;`
- `glk43499_handle_create_default`：`memset target` 后、select 前调用 defaults。
- `publish_active_offsets` 仍只覆盖 p0/init。

## 验证

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

llvm-nm 43499 b/B = 0

important_flatten S1：base=19 cur=19 args_diff=16 PASS
cmp_disasm --important-only：无新增 COUNT/ORDER FAIL
```

## 未完成

- 调用点仍大量使用 target.h 宏；S8b 才逐步替换为 `s->target.layout.*`。
- 未上真机验证 data_addr/p0 路径。

## 文档

- [S8a 设计](s8a-layout-defaults-design.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/important-s8a.log`、`flatten-s8a-s1.log`
