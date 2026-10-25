# P1a–P1d-0 证据：GlkSession 骨架、CLI、运行态搬迁与零句柄（2026-10-21）

> P1d-0 已删除 `g_exploit_session`；下文 P1a/P1b 中“publish/CAS/section”记录保留为历史，不再代表当前 API。

> 从属：[P1 施工设计](../analysis/43499-p1-session-design.md) / [计划 v4](../plan/43499-43284-cpp-parameterization-plan.md)。用户 2026-10-21 批准测试分支自由改码、纯 C 可用。

## 1. 范围

P1a 只新增类型/句柄，不改 15 个核心函数签名，不改旧 main：

| 文件 | 内容 |
|---|---|
| `app/src/main/cpp/gl43499/glk_session.h` | Target/Config/Env/Runtime/Result/Session/WriteRequest/LogSink |
| `app/src/main/cpp/gl43499/glk_session.c` | `g_exploit_session`、target init/valid、create/destroy |
| `app/src/main/cpp/CMakeLists.txt` | `glk_session.c` 加入 `ghostlock43499` target |

- `Glk43499Target` 带 `self` 与 `release[192]`；`offsets.uname_r` 重指到 `release`，禁止按值复制。
- `g_exploit_session` 放在 `.data.glk_session`（链接后落在 `.data`），避免新增 BSS 改变既有全局布局。
- 代码含 `TODO(temp-20261021)`：单会话/纯 C/leak-on-stuck 三项临时接受，P6 前复核。
- `fops.c` 只在 consumer_stuck 泄漏分支加 TODO 注释，不改逻辑；cmp_disasm 仍 PASS。

## 2. 构建与符号

```sh
./gradlew :app:externalNativeBuildDebug --offline --console=plain
# BUILD SUCCESSFUL, rc=0
```

`llvm-nm -S`：

```text
000000000002bcc8 0000000000000008 D g_exploit_session
000000000001e82c T glk43499_target_init
000000000001e960 T glk43499_target_valid
000000000001e9cc T glk_session_create_43499
000000000001ebc8 T glk_session_destroy
```

- 旧 27+13+26 个全局仍在：P1a 只加骨架，P1b/P1c/P1d 才搬。
- `g_exploit_session` 是唯一新增可写全局；旧全局清单见 [附件 B](43499-43284-resource-ownership.md)。

## 3. cmp_disasm（baseline vs P1a）

```sh
python3 tools/cmp_disasm.py --reviewed --partial \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so
# RESULT: PASS, rc=0
```

| 结果 | 数量 | 目标 |
|---|---:|---|
| IDENTICAL (strict) | 4 | owner/waiter/run_main/prepare_good |
| OPERAND-SHIFT (reviewed data displacement) | 11 | consumer、3 route、do_one_write、3 resident、prepare_skb、prepare_kernel、run_exploit |
| SHAPE-DIFF / branch / call / stack | **0** | — |

- 差异全部是新增 `g_exploit_session` 引起的全局数据地址位移；`--reviewed` 只归一化数据地址。
- 第一版把 `g_exploit_session` 放 BSS 时，`prepare_skb_payload` 出现 `str [x9,#0xa80]` → `str [x9]` 的 SHAPE-DIFF；改到 `.data.glk_session` 后消失。**该 section 放置是 P1a 判据的一部分，不得回退到 BSS。**
- 原始日志：`build/analysis/cmp-disasm-p1a-sectioned.log`。

## 4. 未做/下一步

- P1a 不调用 create/destroy；CLI 仍走旧 `run_exploit(argc,argv)`。
- P1b：main.c 27 全局搬入 session、`run_exploit(Glk43499Session *)`、main 改 create/destroy；届时 `cmp_disasm` 允许差异会变大，按 P1 设计 §8 逐目标列理由。
- 无真机：P1a 只证明构建/符号/机器码布局；真机门禁仍 pending。

## 5. P1b-1：CLI create/destroy（2026-10-21）

在 P1a 骨架上把 main 改成：

```c
set_unbuffer();
init_runtime_paths();
if (select_offsets() < 0) return 1;
return run_with_session(argc, argv);
```

- `run_with_session` 用已选中的 `active_offsets` + `uname.release` 构造 `Glk43499Target`，填默认 `Glk43499Config`，create 后调用旧 `run_exploit(argc, argv)`，最后 destroy。
- `run_exploit` 的签名与函数体**未改**；P1b-1 只证明 create/destroy 与 target init 可走通。
- 旧全局仍在：P1b-2/P1c/P1d 才搬。

验证：

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
cmp_disasm --reviewed --partial baseline vs P1b-1  # RESULT: PASS
15/15 OPERAND-SHIFT (data displacement), 0 SHAPE/BRANCH/CALL/STACK
```

日志：`build/analysis/cmp-disasm-p1b1.log`。

## 6. P1b-2a 接口检查点（2026-10-21）

已把 session 参数接入 15 个入口：

```text
waiter/owner/consumer 收 GlkRouteThreadArg / Glk43499Session*
run_main_route_threads(s, req)
do_one_write(s, target, label, mode, leaf) 内部构造 GlkWriteRequest
3 route(s, req)
resident_start/write/stop(s)
prepare_skb/kernel/good(s, req)
run_exploit(s)
```

- 构建：`BUILD SUCCESSFUL, 0 warnings`。
- 行为未改：route/prepare 仍读旧全局；参数暂为后续批次预留。
- `cmp_disasm` strict：15/15 **DIFF（指令数变化）**，strict 仍红；strict 等价未宣称。
- `cmp_disasm --count-delta 32`：**PASS**；delta 1～27（do_one_write/run_exploit 各 +27），全部 ≤ P1 设计 §8 的 32；`calls=subsequence`。
- 调用序列：baseline 的 `bl` 目标在 candidate 中按同序出现；无调用被删除/重排；新增调用仅为局部 helper。
- 结论：**P1b-2a 接口门禁通过（count-delta 32），strict 未过**；P1b-2b 搬运行态全局。

日志：`build/analysis/cmp-disasm-p1b2.log`（strict）、`build/analysis/cmp-disasm-p1b2-delta.log`（count-delta）。

## 7. P1b-2b 运行态搬迁（2026-10-21）

已搬入 session：

```text
f_wait/f_pi_target/f_pi_chain；waiter/owner/consumer 原子；route_done/waiter_tid
punch_consume_go/stop；consumer_calls/success/inflight；main_route_delay_usec
fast_repair_route；route_last_step/errno；t0
```

- `main` 删除上述可写全局；`fops.c` 改为 `s->rt.*`；`common.h` 删除对应 extern。
- `memfd_leak` 仍全局（P1d 搬）；`active_offsets`/`p0_*`/`g_init_cred` 仍全局（P3）。
- 构建：`BUILD SUCCESSFUL, 0 warnings`。

### important_trace

```text
IMPORTANT-SUMMARY functions=55 base=220 cur=221
重要 call/syscall 计数：无 FAIL
重要 call/syscall 顺序：无 FAIL
IMPORTANT-TRACE：20 条 DIFF，全部可解释
```

差异分类：

| 类别 | 例子 | 原因 |
|---|---|---|
| futex 地址 | `waiter/owner/consumer` 的 `futex_op x0` fixed→computed | 全局字移入 `s->rt` |
| pthread_create x3 | NULL→frame/computed | 传 session / `GlkRouteThreadArg` |
| pthread_join x0 | load 240→256 | route 结构布局变化 |
| waitpid/fcntl x0 | load 2928→1752 / 1840→752 | `mm_ctx` 移入 session |
| 新函数 | `run_with_session` | P1b-1 CLI 壳 |

结论：**重要调用/系统调用的数量与顺序不变；参数变化均为 session 指针/布局派生**。strict 仍红，未宣称机器码等价。

日志：`build/analysis/important-p1b2b.log`、`build/analysis/cmp-disasm-p1b2b-important.log`、JSON `build/analysis/important-p1b2b.json`。

## 8. P1c：fops 私有状态搬入 session（2026-10-21）

已搬入 `s->rt`：

```text
mr_l1/mr_l2/mr_cond；mr_tx/mr_ty；mr_* atomics
mr_target/mr_value/mr_lock/mr_task；mr_fd/mr_ready/mr_policy/mr_lock_slot
tcp_punch_go/stop/phase/failed
route_last_step/route_last_errno（P1b-2b 已搬）
```

- `mr_y/mr_x` 收 `Glk43499Session*`；`tcp_punch_state` 携带 session。
- `standard_io_backup` 改为 `do_pselect` 栈局部；`reserve_standard_io(int[3])`。
- `llvm-nm`：fops 的 `mr_*`/`tcp_punch_*` 可写全局为 0。
- 构建：`BUILD SUCCESSFUL, 0 warnings`。

### 门禁

```text
cmp_disasm --count-delta 64：RESULT PASS
important_trace：functions=55 base=220 cur=221
IMPORTANT-COUNT/ORDER FAIL：0
IMPORTANT-TRACE DIFF：53（全部为 session/线程句柄地址或结构偏移）
```

53 条 arg diff 归类：

| 类别 | 原因 |
|---|---|
| `futex_op x0` fixed→computed | `mr_l1/l2/cond` 移入 `s->rt` |
| `pthread_create x0/x1/x3` | `mr_tx/mr_ty` 与 `mr_y/mr_x` 参数改为 session |
| `pthread_join x0` | `mr_tx/mr_ty` 在 session 内的偏移 |
| `fallocate/syscall/setsockopt/close` 参数 | `TcpPunchState` 增加 session 指针，状态在 `rt.tcp_punch_*` |

结论：重要 call/syscall 计数/顺序不变；参数变化均为 session/线程句柄派生。strict 仍红。

日志：`build/analysis/cmp-disasm-p1c.log`、`build/analysis/important-p1c.log`。





