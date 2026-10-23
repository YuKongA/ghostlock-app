# P2 证据：路径与 CPU 配置搬入 session（2026-10-21）

> 从属 [P1 施工设计](../analysis/43499-p1-session-design.md)。目标：删除配置类可写全局。

## 改动

- 删除 `g_home_dir` / `g_root_script_path`：
  - `init_runtime_paths(Glk43499Config *)` 写 `home/root_script/ksu_log`；
  - `write_root_script(const Glk43499Config *)` 只用 cfg 路径；
  - `child_pipes.root_script` 传给 root shell；
  - KSU 日志轮询直接读 `s->config.ksu_log`。
- 删除 `g_core_main` / `g_core_consumer`：
  - `init_cpu_config(Glk43499Config *)` 写 `main_cpu/consumer_cpu`；
  - `CORE(s)` / `CONSUMER_CORE(s)` 宏；
  - `clone_child(core)`、`kernelsnitch_setup(..., core)` 显式收 core。
- `select_offsets(home)` / `try_external_offsets(release, home)` 不再读全局路径。
- `run_with_session` 在 create 前完成 CPU/路径/根脚本/offset 选择。

## 验证

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
nm：g_core_main/g_core_consumer/g_home_dir/g_root_script_path 无 B/D 符号
```

```text
cmp_disasm --count-delta 64：红（prepare_kernel_page shape，参数传递）
IMPORTANT-SUMMARY functions=55 base=220 cur=220
IMPORTANT-COUNT/ORDER FAIL: 0
IMPORTANT-TRACE DIFF: 78（session/config 地址或结构偏移）
```

结论：重要 call/syscall 数量/顺序不变；strict 等价未宣称。

## 剩余可写全局

```text
active_offsets / g_external_offsets / g_external_release / p0_kernel_phys_load / g_init_cred_image -> P3
g_file_buf / g_*_map -> offsets_json P6
kernelsnitch_strings / futex_hashsize -> kernelsnitch P4/P6
known_offsets -> const/relro
```

日志：`build/analysis/cmp-disasm-p2.log`、`build/analysis/important-p2.log`。
