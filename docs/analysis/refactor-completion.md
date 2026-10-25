# 重构完成证据（2026-10-21）

> 目标：43499/43284 C++ 化后的核心攻击函数确认、全局清零、数值参数化、
> 大函数拆分、JNI 化基础；JNI 本体按用户要求不实现。

## 1. 核心攻击链

43499：

```text
run_exploit
  exploit_prelude
  run_phase1_probe
  run_w1_stage
  run_vr_tag_clear
  run_w2_w3_chain
    run_w3_attempts
  run_post_root
```

43284：

```text
exploit
  exploit_patch_chain
  exploit_wait_markers
patch_file_cbc
  cbc_setup / cbc_block / cbc_cleanup
patch_hook
  hook_prepare / hook_write_shell / hook_write_trampoline
```

## 2. 全局清零

```text
ghostlock43499 b/B = 0
ghostlock43284 b/B = 0
ghostlock_jni   b/B = 0
```

2026-10-22 43499 接入 `common/gl_log` 后新增 4 个 DSO-local `d`
（日志库状态，非攻击/会话状态）；`b/B` 仍为 0。

状态全部在调用方拥有的 `Glk43499Session` / `Glk43284State` / handle 内；
无进程级可变句柄。

## 3. 参数化

- 43499 layout：image/p0/direct-map/identity、payload/fake 结构、page_size。
- 43499 config：TCP/pselect/skb 几何、W1/W2/W3 attempts/settle、retry/KSU 等待、
  perf sample period、fast-repair 延迟。
- 43284：ports/SPI/seq/AES key/payload/restore 在 `Glk43284State`。
- target.h/common.h 常量仅作 `glk43499_layout_defaults` / `glk43499_config_defaults` 的默认值。

## 4. 大函数拆分（flatten 门禁）

```text
S1 run_exploit          19/19 PASS
S2 do_tcp               11/11 PASS
S3 do_pselect            4/4 PASS（ignore close）
S4 prepare_kernel_page  17/17 PASS
S5 prepare_skb_payload   0/0 PASS
S6 child_main           24/24 PASS
S7a patch_file_cbc      10/10 PASS
S7b patch_hook           1/1 PASS
S7b exploit              1/1 PASS
```

## 5. JNI 基础（不实现新 JNI）

- 三个 SHARED：`ghostlock43499` / `ghostlock43284` / `ghostlock_jni`。
- C API：handle create/run/destroy；`GlkLogSink`；`GlkFdSink`。
- 现有 JNI 导出：
  `Java_com_ghostlock_app_nativebridge_NativeExploitBridge_create/run/destroy*`
  各 3 个。
- Kotlin 桥存在；`:app:compileDebugKotlin` 仍被项目既有缺失类阻断。

## 6. 构建/测试

```text
./gradlew :app:externalNativeBuildDebug --offline  # 0 warnings
glk_log host test                                  # rc=0
```

## 7. 未完成/未验证

- 冷机 standalone 43499：初始 **FAIL**（KernelSnitch leak），
  [记录](device-gates/20261021-1937-cold-43499-standalone-fail.md)；
  leak/ fd sentinel / W2 修复后冷机 **PASS**：`child is root!` + `KernelSU ready`，
  [PASS 记录](device-gates/20261022-cold-43499-pass.md)。
  单次 PASS；长期成功率仍需多轮冷机统计。
- P6c-2/3/4：日志调用点接入、JNI create(logFd)、真机 fd 写/关闭。
- Kotlin 缺失类与重复 `RunCoordinator 2.kt`。
- `fops 2.c` 为未编译重复文件，未触碰。
- cmp_disasm 的 `IMPORTANT-REMOVED` 为 helper 搬迁；important_flatten 已覆盖计数/顺序。

## 8. 文档索引

- [计划 v4](plan/43499-43284-cpp-parameterization-plan.md)
- [拆分计划](plan/split-large-functions.md)
- [S1](analysis/s1-run-exploit-split.md) … [S8f](analysis/s8f-wait-param.md)
- [P6c-1](analysis/p6c1-log-sink.md)
