# P4/P6 证据：可写全局清零（2026-10-21）

> 从属 [P1 施工设计](../analysis/43499-p1-session-design.md)。用户目标：彻底清除所有全局变量。

## 改动

| 原全局 | 处置 |
|---|---|
| `futex_hashsize` | 删除；改用 `ks->futex_hashsize`，`futex_hash(addr, mm, hashsize)` |
| `kernelsnitch_strings` | `static const char *const`，落入 `.data.rel.ro` |
| `g_file_buf` | 删除；`load_offsets_json(..., file_buf, cap)` 调用方 `malloc(1 MiB)` |
| `known_offsets` / `g_*_map` | 保持 `static const`，`.data.rel.ro` |

## 符号核验

```text
llvm-objdump -t libghostlock43499.so | rg 'known_offsets|g_.*_map|kernelsnitch_strings'
  known_offsets        .data.rel.ro
  kernelsnitch_strings .data.rel.ro
  g_profile_map        .data.rel.ro
  g_symbol_map         .data.rel.ro
  g_task_map           .data.rel.ro
```

`libghostlock43499.so` 的 `llvm-nm` `b/B` 段为 0；43499 的 `.data` 无可写符号，仅 libc 的 `fini_array_with_sentinels`。`libghostlock43284.so` 仍有全局，P5 另批处理。

## 构建与门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
cmp_disasm --count-delta 64：红（已删除/前移调用的基线 bl 非子序列；无新增重要 call）
IMPORTANT-SUMMARY functions=54 base=220 cur=218
IMPORTANT-COUNT/ORDER FAIL: 0
IMPORTANT-TRACE DIFF: 87（target/session 地址、hashsize 参数）
```

结论：43499 可写全局清零；43284 待迁移；strict 等价未宣称。

日志：`build/analysis/cmp-disasm-p4.log`、`build/analysis/important-p4.log`。
