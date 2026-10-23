# P3b 证据：Target 接管 offsets 与 p0/init_cred（2026-10-21）

> 从属 [P1 施工设计](../analysis/43499-p1-session-design.md)。目标：删除 `active_offsets` / `p0_kernel_phys_load` / `g_init_cred_image`。

## 改动

- `Glk43499Layout` 增 `p0_kernel_phys_load` / `init_cred_image`。
- `runtime_struct_offsets.h` 的 `_RSO` 改为读局部 `t`；每个使用宏的函数首行：
  `const struct kernel_offsets *t = &s->target.offsets;`
- `data_addr(target, image)` / `p0_data_alias(target, image)` 收 const Target。
- `publish_active_offsets(target)` 写 target layout，不再写全局。
- `select_offsets(target, home)` 填 target；不再 publish 全局指针。
- `log_startup_context(s)` / `init_p0_profile(target)` 收 session/target。
- `tcp_route_selected(t)` / `kernel5_route_selected(t)` 收 offsets。
- fops 的 mcast/pselect/compact 判断全部改 `t->...`。
- `Glk43499Target` 加 struct tag，消除匿名 typedef 与前置声明冲突。

## 验证

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
nm：active_offsets / p0_kernel_phys_load / g_init_cred_image 无 B/D 符号
IMPORTANT-SUMMARY functions=54 base=220 cur=219
IMPORTANT-COUNT/ORDER FAIL: 0
IMPORTANT-TRACE DIFF: 78（target/session 地址或结构偏移）
```

结论：重要 call/syscall 数量/顺序不变。strict/count 红的原因是 `do_one_write` / `run_exploit` 的基线调用 `set_pselect_write_mode`、`init_cpu_config`、`init_runtime_paths`、`write_root_script` 已删除或前移，基线 bl 序列不再是子序列；不是重要 call 增删。strict 等价未宣称。

## 剩余可写全局

```text
futex_hashsize / kernelsnitch_strings   -> kernelsnitch P4/P6
g_file_buf / g_*_map                     -> offsets_json P6
known_offsets                             -> const/relro
fini_array_with_sentinels                 -> libc
```

日志：`build/analysis/cmp-disasm-p3b.log`、`build/analysis/important-p3b.log`。
