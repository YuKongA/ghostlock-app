# P5a 证据：43284 状态搬入 session（2026-10-21）

> 从属 [P1 施工设计](../analysis/43499-p1-session-design.md)。目标：43284 的可写全局清零。

## 改动

- 新增 `Glk43284State` / `Glk43284PatchRestore`（`glk43284_state.h`）：
  `encap_port/sender_port/spi/seq/aes_key/payload/ko_target/restore`。
- `exp.c` 删除 `g_encap_port`/`g_sender_port`/`g_spi`/`g_aes_key`/`g_seq`/`g_libcxx_r`/`libcxx_ko_target`。
- 所有攻击函数收 `Glk43284State *st`：`compute_iv`、`spawn_crash_dump`、`read_vendor_content`、`do_one_write_cbc`、`patch_file_cbc`、`patch_helper`、`patch_ko`、`patch_hook`、`cleanup`、`setup`、`exploit`。
- `main` 用栈 `Glk43284State`；无进程级句柄。
- `libcxx.S` 从 `.data` 移到 `.rodata`；`setup` 把 payload 复制到 `st->payload`，ko target 写入副本，行为不变。
- `df_log.c` 的 `g_min` 改为 `const`；删除未使用的 `df_log_set_min`。

## 符号

```text
libghostlock43284.so：llvm-nm b/B = 0；无 D 段
剩余 d：
  detect_ko_target.candidates   .data.rel.ro
  exploit.markers               .data.rel.ro
  read_vendor_content.exit_meanings .data.rel.ro
  select_ko_image.imgs          .data.rel.ro
  fini_array_with_sentinels     .data（libc）
```

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
43284 important：functions=12 base=52 cur=52
IMPORTANT-RESULT: PASS（无 COUNT/ORDER/TRACE DIFF）
43284 count-delta：红，仅 patch_ko SHAPE-DIFF（函数收 st 参数）
```

结论：43284 可写全局清零，重要调用序列不变；strict 等价未宣称。

日志：`build/analysis/cmp-disasm-43284-state.log`、`build/analysis/important-43284-state.log`。
