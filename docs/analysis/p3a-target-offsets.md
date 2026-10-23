# P3a 证据：external offsets 搬入 Target（2026-10-21）

> 从属 [P1 施工设计](../analysis/43499-p1-session-design.md)。目标：删除 `g_external_offsets` / `g_external_release`。

## 改动

- `select_offsets(Glk43499Target *target, const char *home)` 填 target，不再写全局 external。
- `try_external_offsets(release, home, struct kernel_offsets *out, char *release_buf, cap)`：局部 `imported` / `imported_release`。
- `target_store_offsets()` 在 target 原地复制 offsets/release，重指 `uname_r`，设置 `self`。
- `run_with_session`：先建 target，再 `select_offsets(&target, home)`，然后 create。
- `run_exploit` 的 fallback 用 `select_offsets(&s->target, s->config.home)`。

`active_offsets` 仍临时指向 `target->offsets`，供 `_RSO` 宏使用；P3b 删除。

## 验证

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
nm：g_external_offsets / g_external_release 无 B/D 符号
IMPORTANT-SUMMARY functions=54 base=220 cur=219
IMPORTANT-COUNT/ORDER FAIL: 0
IMPORTANT-TRACE DIFF: 78
```

结论：重要 call/syscall 顺序不变；strict 等价未宣称。

## 剩余可写全局

```text
active_offsets
g_init_cred_image
p0_kernel_phys_load
g_file_buf / g_*_map
kernelsnitch_strings / futex_hashsize
known_offsets（const/relro）
```

日志：`build/analysis/cmp-disasm-p3a.log`、`build/analysis/important-p3a.log`。
