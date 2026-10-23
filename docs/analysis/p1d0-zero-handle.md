# P1d-0 证据：删除进程级句柄全局（2026-10-21）

> 从属 [P1 施工设计](../analysis/43499-p1-session-design.md)。用户 2026-10-21 目标：彻底清除所有全局变量。

## 改动

- 删除 `atomic_uintptr_t g_exploit_session` 及其 `.data.glk_session` section。
- 删除 `GLK_BUSY`；`glk_session_create_43499` 不再 publish/CAS，只初始化 `outer`。
- `glk_session_destroy` 只清零调用方的 `outer`，不触碰进程级指针。
- JNI 将用 `jlong` 句柄；CLI 用栈 `GlkSession`；生命周期由调用方持有。

## 验证

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
llvm-nm ... | rg g_exploit_session                # 无输出
cmp_disasm --reviewed --partial --count-delta 64  # RESULT PASS
IMPORTANT-SUMMARY functions=55 base=220 cur=221
IMPORTANT-COUNT/ORDER FAIL: 0
IMPORTANT-TRACE DIFF: 53（session/线程句柄地址或结构偏移）
```

- 日志：`build/analysis/cmp-disasm-p1d0.log`、`build/analysis/important-p1d0.log`。
- `run_with_session` 仍是唯一新增函数；CLI 不依赖任何全局句柄。

## 剩余

- P1d：`util.c` 的页/喷溅/fd + `memfd_leak`。
- P2/P3：`g_core_main/consumer`、`active_offsets`、`p0_kernel_phys_load`、`g_init_cred_image`。
- 最终 `llvm-nm` 的 `b/B/d/D` 段必须为空（启动期只读常量不计）。
