# cmp_disasm 基线记录（2026-10-21）

> 计划：[43499-43284-cpp-parameterization-plan.md](../plan/43499-43284-cpp-parameterization-plan.md) P0。HEAD `d2778a1a`。本记录只证明工具能解析 28 个核心符号并给出可复跑基线；**不是** baseline vs candidate 的等价结论。

## 产物与哈希

| 文件 | sha256 | 大小 |
|---|---|---:|
| `build/native/baseline-20261021/libghostlock43499.so` | `e0fc7fe50034bca3adb8d579f0a8988202e4bfd0275964db481b901e3af9c475` | 266,152 B |
| `build/native/baseline-20261021/libghostlock43284.so` | `5d922ffb8e0cf176d6216d20d254f8ecdf799340139d8e0f7eb8b32aa0c8b9f4` | 1,986,184 B |

原始构建产物：`build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/`。构建命令：`./gradlew :app:externalNativeBuildDebug --offline`（退出码 0）。基线目录不提交，`clean` 会删除；哈希与日志保留在本记录及 `build/analysis/`。

## 工具改动（P0）

- `tools/cmp_disasm.py` 的 `TARGETS` 由 6 组扩到 28 组：43499 15（含 3 个 resident、prepare_*、run_exploit），43284 13（含 compute_iv/find_hook_target）。
- 新增 `--partial`：目标在**两个二进制都不存在**时打印 `SKIP` 并继续；只在一侧缺失仍 `MISSING` 失败。用于一个二进制只含一个 backend 的自比。
- 比较语义、`--reviewed` 规则、relocation 归一化均未改。
- 自测：`python3 tools/tests/test_cmp_disasm.py` → 13/13 OK。

## 复跑命令与结果

```sh
export LLVM_OBJDUMP=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-objdump
python3 tools/cmp_disasm.py --partial <43499.so> <43499.so>   # rc=0
python3 tools/cmp_disasm.py --partial <43284.so> <43284.so>   # rc=0
```

| 运行 | IDENTICAL | SKIP | RESULT | rc |
|---|---:|---:|---|---:|
| 43499 自比 | 15 | 13 | PASS | 0 |
| 43284 自比 | 13 | 15 | PASS | 0 |

原始日志：`build/analysis/cmp-disasm-43499-baseline.log`、`build/analysis/cmp-disasm-43284-baseline.log`；退出码同目录 `.rc` 文件为 0。

## 符号来源

`llvm-nm --defined-only` 实测：43499 命中 15 个、43284 命中 13 个；两个命令退出码 0。示例：`T waiter_thread`、`t do_one_write`、`T run_main_route_threads`、`T find_hook_target`、`t patch_hook`、`t compute_iv`。

`readelf` 在本机不存在（`command not found`），不能作为 stripped 证据；以 `llvm-nm`/`llvm-objdump` 为准。

## 指令数（strict，实测）

| 目标 | insn | 目标 | insn |
|---|---:|---|---:|
| waiter_thread | 205 | spawn_crash_dump | 137 |
| owner_thread | 84 | read_vendor_content | 172 |
| consumer_thread | 244 | do_one_write_cbc | 154 |
| run_main_route_threads | 254 | patch_file_cbc | 341 |
| do_kernel5_fake_lock_route | 225 | patch_helper | 140 |
| do_tcp_fake_lock_route | 738 | patch_ko | 123 |
| do_pselect_fake_lock_route | 467 | patch_hook | 372 |
| do_one_write | 155 | find_hook_target | 832 |
| kernel5_resident_start | 288 | createOrphanProcess | 71 |
| kernel5_resident_write | 57 | exploit | 162 |
| kernel5_resident_stop | 35 | setup | 254 |

> `kernel5_resident_stop` 的符号大小是 `0x8c` = 140 **字节**，不是 140 条指令；`cmp_disasm`/`llvm-objdump` 逐条计数为 35（0x8c/4=35）。
| prepare_skb_payload | 741 | cleanup | 58 |
| prepare_kernel_page | 1110 | compute_iv | 44 |
| prepare_good_kernel_page | 94 | — | — |
| run_exploit | 1383 | — | — |

## 边界

- 本记录只用于 P1+ 的 `cmp_disasm` 对照；真机门禁仍是攻击路径唯一权威（AGENTS §2/§8）。
- 自比 PASS 不代表候选实现等价；每批必须 baseline vs candidate，并给出 strict/reviewed 差异分类。
- 若基线目录被 `clean` 删除，用同一 HEAD/同一命令重建；哈希不一致先查产物新鲜度，不继续。
