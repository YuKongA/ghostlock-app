# 43499 / 43284 核心参数化与 JNI 化计划（v4）

> **状态**：**P1 实施中**（用户 2026-10-21 临时批准：测试分支、git 备份、可自由改码、纯 C 可用、例外需注释）。P0a/P0b 完成。
> **2026-10-22 冻结**：very_legacy 拆分路线判定成本过高；JNI 重写改见 [43499-43284-jni-rewrite-plan.md](43499-43284-jni-rewrite-plan.md)（待批）。
> **边界裁定（C1，v4，已批准）**：本件修改现有 app-owned `ghostlock43499`/`ghostlock43284` target；`externalNativeBuild` 会打包为 `lib*.so`，属生产路径改动。用户已在测试分支批准；每批真机门禁仍是合入前提，未过只标 `device-gate-pending`。`jni-two-backend-plan` 仅历史参考。
> **快照**：HEAD `d2778a1a`，2026-10-21T20:30Z。基线：`build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so` sha256 `e0fc7fe5…f9c475`；`libghostlock43284.so` sha256 `5d922ffb…c8b9f4`。两者均带调试符号（`llvm-nm` 可见 `waiter_thread`/`patch_hook`），非 stripped。
> **附件**：[核心清单与全局/字面量盘点](../analysis/43499-43284-core-inventory.md)（≤8 KiB）。本件只留决策与指针。

## 0. 目标与完成定义

用户 2026-10-21 目标：确认核心攻击函数；记录 cmp_disasm；清理固定值与全局；参数化函数数值；确立所有权；为 JNI 化奠基。瀑布流：计划 → design-critic → 修订 → 实现。

| # | 判据 | 证据 |
|---|---|---|
| D0 | 28 个核心函数逐行冻结（签名/行号/指令数/资源闭包） | 附件 A |
| D1 | cmp_disasm 能解析全部目标；每批 baseline vs candidate 有差异分类 | P0 + 每批日志 |
| D2 | **零可写全局**；session 由调用方持有，线程只借用会话 | `llvm-nm` + 源码扫描 |
| D3 | 目标/策略值全部来自 Target/Config；剩余数字在字面量清单逐行分类 | 附件 C + P0 生成 TSV |
| D4 | 每个 fd/pid/映射/线程/信号/缓冲区有 owner 与终结点；唯一例外 `leak-on-stuck` 显式记录 | 附件 B |
| D5 | JNI create/run/destroy 骨架可构建、不打包、不接生产 | P5 符号/APK 证据 |
| D6 | UML、introduction、计划指针同批更新 | 文档 diff |

## 1. 事实基线

### 1.1 核心攻击函数

43499 共 15 个目标（waiter/owner/consumer/run_main/3 route/do_one_write/3 resident/prepare_skb/prepare_kernel/prepare_good/run_exploit）；43284 共 13 个目标（spawn_crash_dump/read_vendor_content/do_one_write_cbc/patch_file_cbc/patch_helper/patch_ko/patch_hook/find_hook_target/createOrphanProcess/exploit/setup/cleanup/compute_iv）。逐行签名、file:line、指令数、C/汇编、是否可 cmp 见附件 A。`libcxx_start` 作为汇编目标单列，不计入 28 个 C 目标；若 P4 要比较它，另加 asm 目标。

### 1.2 cmp_disasm 现状

实测：`python3 tools/cmp_disasm.py <43499.so> <43499.so>` → 6/6 MISSING / FAIL。原因是 `TARGETS` 只列旧 C++ 修饰名，当前 C 符号是裸名；不是二进制 stripped（`llvm-nm` 已核对，见附件 A 的命令与输出）。T0 只扩展候选表：43499 加 15 个裸名，43284 加 13 个；不改比较语义、不改 `--reviewed` 规则。验收 43499 15/15 + 43284 13/13（合计 28）IDENTICAL (strict)；`--partial` 跳过另一 backend。自比仅作工具 smoke；D1 的真实证据是每批 baseline vs candidate。

### 1.3 全局与固定值

附件 B 给出精确表：43499 `main.c` 27 个可变全局、`fops.c` 13、`util.c` 26；43284 `exp.c` **6** 个（`g_encap_port`、`g_sender_port`、`g_spi`、`g_aes_key`、`g_seq`、`g_libcxx_r`）；`bootstrap.c` 4 个路径常量；root script/perf/sigaction 单列。附件 C 给出数字字面量分类与 P0 生成 `build/analysis/literals.tsv` 的命令（`tools/glk_literal_inventory.py`，P0b 实测 2,558 条：A163/B236/C220/D33/E1246/L660/?0，104 条人工覆盖）；D3 不接受“≈”，E 类按具名类别复核，见附件 C。

越权 env：`GHOSTLOCK_CORE`、`GHOSTLOCK_CONSUMER_CORE`、`GHOSTLOCK_5X_RESIDENT`、`GHOSTLOCK_5X_PHASE1_PROBE`、`GHOSTLOCK_W1_ONLY`、`GHOSTLOCK_TCP_ROUTE` 必须变参数；只保留 `GHOSTLOCK_HOME`/`TMPDIR`/`GHOSTLOCK_KSU_LOG`（AGENTS §4）。

## 2. 设计

### 2.1 单会话

- 进程内只允许一个会话（43499 或 43284）；两者都改内核/进程全局，并发无语义。
- **不设进程级句柄全局**：`GlkSession` 由调用方持有；JNI 用 jlong 句柄，CLI 用栈；create 只初始化，不 publish。
- 所有原全局状态迁入会话；线程入口收 `Glk43499Session *`。目标 = 可写全局清零（当前剩余项逐批列在 P1d/P3）。

### 2.2 唯一读取路径与命名（C4/C13）

| 概念 | canonical | 定义 | 规则 |
|---|---|---|---|
| 会话 | `GlkSession` / `Glk43499Session` / `Glk43284Session` | `glk_session.h` | 一个 backend 一个 payload；无别名 |
| 目标 | `Glk43499Target` | `glk43499_types.h` | 嵌入 `struct kernel_offsets` + `Glk43499Layout`；**唯一运行期读取面** |
| 配置 | `Glk43499Config` / `Glk43284Config` | 同上 | 只读；create 时复制 |
| 运行态 | `Glk43499Runtime` / `Glk43284Runtime` | 同上 | 仅核心读写，不跨 JNI |
| 环境 | `Glk43499Env` | 同上 | Kotlin 探测后传入，核心不读 /proc/env |

- `active_offsets` 只在 create 初始化时读一次，之后只读 `target.kernel_offsets`；函数不得再展开 `target.h` 宏，也不得读 `runtime_struct_offsets.h` 的 `_RSO`。
- `target.h` 只作**默认常量表**（生成/手维护一处）；`Glk43499Layout` 与 `kernel_offsets` 逐字段映射，P1 加 `static_assert`/offset 表等价测试。
- `Glk43499Layout` 字段：地址基址、direct-map/vmemmap、payload 布局（lock/w0/fops/right/left/fake_task/cred_copy/tcp_*）、skb delta/bias、fake-waiter/fake-task 字段偏移。PAGE_SHIFT/PAGE_SIZE/direct-map/vmemmap 归 Target（C5），不归 E。

### 2.3 参数分类（C5）

| 类 | 含义 | 落点 | 例子 |
|---|---|---|---|
| A Target | 设备/内核/页/布局 | `Glk43499Target` | init_task_off、task_cred、mcast_waiter_off、lock_off、PAGE_SHIFT、direct_map_end |
| B Config | 策略/调优/重试/时序 | `Glk43499Config` | w1_attempts、select_timeout_us、tcp_arm_seq、CPU 对、route |
| C Runtime | 一次会话的活状态 | `Glk43499Runtime` | futex 字、原子、线程、页基址、fd/pid、g_seq |
| D Env | 调用方探测 | `Glk43499Env` | SELinux、seccomp、KSU、zygote |
| E ABI/算法 | 不可运行时改变 | 具名 enum/static const | syscall 号、ELF 常量、AES 块 16、SBOX |

`Glk43499Config`：CPU 对、route、resident、W1/W2/W3 attempts/settle/rounds、race wait/poll、select nfds/timeout/enter_delay/nice/burst/max/settle、tcp attempts/arm/hold/punch_len、heap attempts/deadline、mm order/partials/collisions、slab waves/batch、perf period/pages/spin、vr_bypass、w1_only、log_level、home/script/ksu_log 路径。
`Glk43284Config`：port/spi/key、ko_target、libcxx/crash_dump 路径、hook_symbol、shellcode_data/len、markers[]/count、poll/timeout/trigger_sleep、restore_on_exit；`g_seq` 归 Runtime。

### 2.4 所有权（C6）

完整逐项表（每个 fd/pid/mmap/thread/signal，含 `mr_fd`、`punch_fd`/`map`、`listener/client_fd/server_fd`、`pipefd`、`perf_fd`/mmap、43284 `sk_send/file_fd/pfd`）见 [resource-ownership](../analysis/43499-43284-resource-ownership.md)。顺序不变量：waiter 停止访问 → route_done → unlock chain → join 全部线程 → 关闭 reclaim/socket → 释放页/映射；PI waiter 的“停止访问 → 解除关联 → 回收”逐字保留，提前 close/unmap = 阻断。

**D4 唯一例外 `leak-on-stuck`**：pselect 的 `pipefd[2]`/`block_fd` 在 `consumer_inflight != 0` 超过 2s 时**故意不关**（fops.c:690-724，`route_last_step=34`）；关闭会回收 in-flight syscall 仍引用的 pipe 对象。owner=session，终结点=进程退出，不做 destroy close。**用户 2026-10-21 临时接受，代码加 `TODO(temp-20261021)` 注释**；其余 fd 不得引用。

### 2.5 JNI 形状（C7/C10）

~~~c
typedef struct { uint32_t magic, version, size; } GlkPodHeader;
jlong glk43499Create(JNIEnv*, jclass, jbyteArray targetBlob, jbyteArray configBlob);
jint  glk43499Run(jlong handle, jlong target, jint mode, jint leaf, jstring label);
jint  glk43499Destroy(jlong handle);
jlong glk43284Create(JNIEnv*, jclass, jbyteArray configBlob);
jint  glk43284Run(jlong handle);
jint  glk43284Destroy(jlong handle);
~~~

- POD header 带 magic/version/size；短/未知/超限 blob 具名拒绝 `bad-parameters:*`。
- handle = generation + state + backend；stale/double-destroy/run-before-create/busy 各有具名错误；destroy 幂等，先停线程再清句柄。
- `target` 原始地址在 Run 入口校验 canonical/direct-map 区间；越界 `bad-target`。
- 日志走 `GlkLogSink` 环形缓冲（容量/overflow/owner 明确），Run 返回后读取；PI 内零 JNI 回调。
- Run 同步；取消只置 stop 并返回 `cancelled`，不 pthread_cancel。
- P5 CMake 新增 `glk43499jni`/`glk43284jni` SHARED，输出 `libglk43499jni.so` 等；`packaging.jniLibs.excludes` 排除，`unzip -l` 证明 APK 不含；P6 删 target/文件。

## 3. 瀑布批次

| 批 | 内容 | 门禁 | 回滚 |
|---|---|---|---|
| P0a | 工具候选表 + 28 行清单 + 基线副本/哈希 + 字面量生成器 | 15/15 + 13/13 自比 PASS；不改攻击源码；raw log/退出码/sha | 删候选行/脚本 |
| P0b | 清 `?` + E 具名类别复核 + C/D 分类抽检 | D3：`?`=0（104 条人工覆盖）；E 类别可指名 | 回滚 TSV/分类器 |
| P1 | GlkSession 骨架 + 全局搬入 + 函数收 ctx；不改控制流 | NDK 零告警；cmp_disasm 逐函数差异表；真机（待设备） | 恢复全局声明 |
| P2 | env → Config；重试/超时/CPU/日志/route | 无越权 getenv；默认值等价；cmp_disasm | 恢复 env |
| P3 | target.h/runtime_struct_offsets → Target 唯一读取 | 函数内无 target 宏；字段等价测试；cmp_disasm；真机 | 恢复宏 |
| P4 | 43284 6 全局 → session；argv → Config；路径/标记/超时/shellcode data | NDK；13 目标 cmp_disasm；真机 | 恢复 argv/全局 |
| P5 | JNI 骨架 + POD 校验 + generation + 日志环；不接生产 | 构建/符号/APK 不含/ABI 测试 | 删 JNI/CMake |
| P6 | 删旧宏/兼容层；UML/introduction/计划指针 | 全量构建/文档链接/无临时物 | 按批恢复 |

每批一类事；P1–P3 同一文件单 writer。每批留 cmp_disasm 原始日志、构建退出码、`llvm-nm` 全局清单；真机未过不得标 supported，不得打包。

**P0 状态（2026-10-21）**：**P0a/P0b 完成**。证据：[cmp-disasm 基线记录](../analysis/cmp-disasm-baseline-20261021.md)、`build/analysis/literals.tsv`（2,558 条：A163/B236/C220/D33/E1246/L660/?0，104 manual）、`build/native/baseline-20261021/`。P1 已获用户批准；C8 因 AGENTS §4 删除 C++23 条文关闭。

**P1–P5a 状态（2026-10-21）**：43499 P1a…P4 完成，Target/Session 持有全部状态；43284 P5a 完成，`Glk43284State` 持有端口/SPI/AES/seq/restore，payload 复制到 state，`libcxx.S` 移入 `.rodata`，`df_log.g_min` 改 const。两个 .so 的 `llvm-nm` `b/B`=0，43284 无 D 段；剩余仅 `.data.rel.ro` 常量与 libc。43499 important 54/220→218、无 COUNT/ORDER FAIL、87 arg diff；43284 important 12/52→52 PASS。构建 rc=0/0 warnings。P5b C API 完成：`glk43499_handle_*`/`glk43284_handle_*` 堆句柄，无全局。P6a 完成：三个 SHARED（43499/43284/ghostlock_jni），hidden 内部符号，导出 API + `attachFd*`；`attachFd*` 暂为 no-op。P6b 完成 JNI 会话/jlong/Kotlin bridge（Kotlin 编译阻塞）；P6c 设计阻断 0；S1–S7 + S8a–S8f 完成；证据：../analysis/refactor-completion.md。记录：[P4](../analysis/p4-zero-globals.md)、[P5a](../analysis/p5a-43284-state.md)、[P5b](../analysis/p5b-c-api.md)。

## 4. 判据

- 构建：`./gradlew :app:externalNativeBuildDebug --offline` 退出码 0；P1 起 C 目标加 `-Wall -Wextra -Wconversion -Wsign-conversion`，新码零告警。
- 静态：getenv 只允许 `GHOSTLOCK_HOME`/`TMPDIR`/`GHOSTLOCK_KSU_LOG`；`llvm-nm -S` 的 `b/B/d/D` 段最终必须为空（目标零可写全局）；字面量 TSV 逐行分类。
- cmp_disasm：P0 自比 smoke；每批 baseline vs candidate 的 strict/reviewed 差异分类；禁止差异（控制流/顺序）即停批。
- 真机：43499 冷机/固定 CPU 对/单 route/KSU 未加载；43284 按 B 系列；归档 ≤8 KB/份。无真机只到 P0，P1+ 标 `device-gate-pending`。
- 文档：full-process-uml 与 introduction §一/§五/§八 同批更新。

## 5. CBN

| 决策点 | 备选 | 收益（数字） | 成本/边际 | 翻转条件 | 结论 |
|---|---|---|---|---|---|
| 语言 | A 纯 C（核心 + JNI）；B 全量 C++23 | A 0 语义改动、28 函数可对拍；B 130KB 重写 | A 0 新语言层；B 无法逐函数对拍 | 用户要求 C++ | A（用户 2026-10-21：纯 C 可用） |
| 会话数 | A 单会话；B 多会话 | A 1 全局；B 0 全局但需锁 | A busy；B N 份页/fd | 并行设备需求 | A |
| 参数形态 | A 扁平 POD；B 逐参 | A 1 次 JNI、60+ 字段可断言 | A 1 codec；B +1 参数/字段 | 跨版本演进 | A，header version |
| 常量 | A 全运行期；B Target/Config + ABI 具名 | A 无数字但热路径带参；B 0 开销 | A +寄存器/栈；B 分类审计 | 用户要求字面零数字 | B，E 类集中 |
| 布局来源 | A `kernel_offsets` + Layout；B 新 profile | A 1 表、已有 JSON/extractor | A 加 Layout/等价测试；B 第二权威 | 非内核布局 | A |

## 6. 风险 / 技术债

| # | 风险 | 处置 |
|---|---|---|
| R1 | 全局搬会话改机器码/时序 | 每批 cmp_disasm + 真机；顺序不变量逐条；失败停批 |
| R2 | 默认值被改 | 默认表只搬不改；基线日志逐字段对拍 |
| R3 | 多会话/重入 | busy；destroy 幂等；join 后清句柄 |
| R4 | 43284 shellcode data 覆盖 | offset/size 参数化；64B 容量校验；恢复原字节 |
| R5 | 无真机 | P0 可完成；P1+ 标 pending，不得称完成 |
| R6 | 与 kt-step 冲突 | 用户 2026-10-21 在测试分支批准本件取代 §3；P6 后复核生产接线 |

技术债：T-LANG 已消解（AGENTS §4 删除 C++23 条文；ADR-0019 撤回）；T-DEVICE（真机门禁）；T-JNI（P5 骨架）。

## 7. 审查记录（v1→v3）

| 编号 | 首轮结论 | v3 处置 |
|---|---|---|
| C1 | 阻断：与 kt-step 第二权威 | 用户 2026-10-21 测试分支批准生产改动；P1 启动 |
| C2 | 阻断：基线无符号 | **事实不成立**：`llvm-nm` 可见符号，`readelf` 在本机不存在（command not found）；P0 附命令与输出 |
| C3/C12 | 阻断/重要：清单不一致 | 附件 A 改为 28 行唯一清单；libcxx_start 单列 asm；compute_iv 纳入 43284 |
| C4 | 阻断：多读取路径 | §2.2 唯一 Target；active_offsets 只读一次；target.h 仅默认表；等价测试 |
| C5 | 阻断：E 类过宽 | PAGE/direct-map/vmemmap 归 Target；E 仅 ABI/算法；字面量 TSV 验收 |
| C6 | 阻断：所有权缺口 | 新增 [resource-ownership](../analysis/43499-43284-resource-ownership.md)，逐 fd/pid/mmap/thread/signal |
| C7 | 重要：JNI 边界 | header/generation/stale/busy/errors/log ring 已补 |
| C8 | 阻断：C11 vs C++23 | AGENTS §4 删除 C++23 条文；用户明确纯 C 可用；C8 关闭，ADR-0019 撤回 |
| C9 | 重要：P0 证据不足 | P0 自比仅 smoke；D1 改为每批 baseline vs candidate |
| C10 | 重要：CMake/打包 | P5 指定 target/输出/排除/符号验收 |
| C11 | 重要：计数/字段 | 43284 改 6 全局；g_seq 归 Runtime；bootstrap/root script 补入附件 |
| C13 | 重要：命名 | §2.2 canonical 表；无别名 |
| C14 | 建议：配额 | 清单移附件；计划 ≤16 KiB |
| C15 | 重要：版本标识 | 标题/记录统一为 v3；冻结哈希以审查记录/提交为准（本文件约 14.9 KB） |
| C16 | 重要：pipefd 泄漏 | D4 例外 `leak-on-stuck`；用户 2026-10-21 临时接受；代码注释 `TODO(temp-20261021)` |
| C17 | 重要：stop 指令数 | **事实不成立**：`kernel5_resident_stop` 35 insn（0x8c/4=35；cmp_disasm 35）；140 是字节数 |
| C18 | 阻断：P0 分类未完成 | P0b 已清：`?`=0，104 manual，E 抽检；P0a/P0b 完成 |

**批准 gate**：C1/C8 已由用户 2026-10-21 关闭；C16 临时接受并注释；P1 启动。C7/C10/C11/C12/C13 在对应批次前关闭；每批真机未过只标 pending。
