# 归档说明

- 原路径：`docs/plan/43499-native-parameterization-v2-plan.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：old backend/JNI/execution authority; preserve as history；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# 43499 Native 全参数化与 ABI v4 计划（R30）

- 状态：**用户已批准方向与范围（approval-status §6.5）**；用户 2026-10-27「直接推进」后 R0 文档 gate 知情豁免，**R2a/R2b/R2c 已实现**；**R3a Native 三路修复 + R3b 三路 Kotlin generator/配置/RequestFactory 也在工作树实现并通过机器门禁（未 commit）**；**R3d 多写/fast-repair/Credential generator/pselect 重喷已实现（Kotlin；Native ABI diff=0）**；**R3f（finish 交 rooted victim 执行 + 拆 W1 墙，task-45）已实现（纯 Kotlin；Native 零改动）**；真机门禁延 P0c（知情强制），host harness 未交付 = 技术债（§7）。证据见 §5/§10 与 [三路实现记录](../analysis/43499-three-route-line-review-20261027.md) §5/§6。
- 日期：2026-10-27；用户裁定见 [approval-status](43499-approval-status.md) §6.5
- 快照：HEAD `a9af5482`（2.0.11）+ 工作树未提交改动（P0a/P0b 半成品标记“待复核未交付”）
- 取代/并入：本计划并入 `43499-43284-cpp-parameterization-plan.md` 的 43499 Native 参数化段（该件 43499 部分声明由本件取代）；与 `43499-flow-attack-readiness-plan-20261026.md` 的 P1–P2 对齐，本件为其“输入生成”前置批。
- 参考评审：`docs/analysis/43499-attack-vs-legacy-review-20261026.md`（D1–D15）、design-critic P0a 评审（阻断 B1–B7/I1–I9）。

## 0. 用户裁定（2026-10-27）

1. **删除全部 reserved 槽**，不再为“以后加字段”预留空间（同 APK 内 JNI↔.so 同构建，无跨版本二进制兼容需求）。
2. **全部参数化**：包括运行期身份/协议位（`pid`、FUTEX_WAITERS `0x80000000`、LEAK_TAG nibble/shift、帧/映射/上下文容量）与所有 Native 字面量。
3. debug 配置移到**专用 debug source set**（release 不打包、release 拒绝加载）。
4. 真机门禁**缺席**：攻击路径改动允许先落代码，**P0c 补真机冷机门禁**；知情的豁免登记技术债（owner=Lead，偿还=P0c）。
5. 范围＝**所有 Native 字面量**（core/*.c、core/*.h、jni43499.c），不只 kernel_internal.h 宏。

## 1. 字面量规则（闭合判据）

- 允许保留（登记理由）：`0/1/-1/-2`（循环/哨兵/数组基址）、`sizeof/offsetof/_Static_assert` 派生、系统头常量（`FD_SETSIZE`、`AF_*`、`INT_MAX`、`socklen_t` 尺寸等）、wire 协议结构由 spec 参数承载的**尺寸**。
- 其余全部进请求字段；**任何未分类字面量 ⇒ 阻断**（扫描脚本 exit≠0）。
- 方法：复用 `build/analysis/literals.tsv`（2,558 条）+ fresh scan（`rg -n '[^A-Za-z_](0x[0-9a-fA-F]+|[0-9]{2,})' core jni43499.c`），产出闭表：`文件:行|字面量|类别(参数/结构/协议/系统/哨兵)|去向字段|消费者|校验`；验收 `unclassified=0` 且允许清单逐条有理由。

## 2. ABI v4（显式、无 reserved）

- 删除 `placement.h/page.h/actor.h/primitive.h` 内全部 `reserved[]`；字段显式命名并尾部追加。
- 每个请求结构含 `struct_size` + `abi_version`（值=4）；JNI 编组时填 `sizeof`，Native 严格 `==` 校验；不匹配 ⇒ `INVALID_ARGUMENT` fail-closed，不得回退默认。
- 参数清单（新增/补齐）：
  - Placement：page_size、allocation_size、slab_order、mm_struct_size、objects_per_slab、mm_partials、prepare_slabs、snitch_collisions、scan_lower/upper/step/limit、measurement/poll/cpu、staging 容量、identity_start/end、leak tag nibble/shift、fill_byte
  - Page/Landing：size、内容指针/长度、reclaim_payload_size、socket_send_buffer_size、reclaim_send_limit、fill_byte
  - Actor：三 CPU/三优先级、setup/settle、poll_us、consumer_max/burst、lock_pi_spin_ns、requeue_expected、disarm_spin_limit、timeouts、nice 轮转范围（本批是否实现轮转见 §4）
  - Multicast：frame_size、socket_buffer_size、arm_hold_ns、arm_sequence、attempts/operation_budget、timeout、family
  - Pselect（仅参数化，不启用）：descriptor_capacity、nfds 上/下限、per_attempt_delay_ns、timeout
  - TCP（仅参数化，不启用）：mapping_size、frame_size、receive_size、mapping_offset、punch_spin、post_hold_ns、zc_address_offset、fill_byte、operation_budget、timeout
  - Ghost disarm（D3）：`disarm_required`、`futex_waiter_flag`、`ghost_pid`（Kotlin 传 `Process.myPid()`）；`FAILURE→不做` 语义不变
  - 安全上限：Native **不内置**目标上限；请求值做跨字段不变量校验（如 `page_size<<slab_order==allocation_size`、`objects*slabs` 计数公式、`reclaim_payload_size==2*allocation_size`、`mapping_offset==page_size`、`zc offset` 对齐/界内）；结构性上限只用系统头派生值或用户批准登记的容量字段。
- 静态断言迁移：原 `kernel_internal.h` 6 条 `_Static_assert` → 运行时校验列表（附负例测试）。

## 3. Kotlin 侧

- `Native43499Jni` DTO：全字段 + `struct_size/abi_version`；与 C 字段名/顺序/大小对拍测试（Kotlin↔C 编译期/单测）。
- 删除 `Cve43499PhysicalConstants` 及 `PlacementConfig/LandingConfig` 的默认值回退；必填构造参数，缺失/非法 fail-closed。
- 新增 data 层 debug loader（复用 `A301SoDebugConfig`）：读 **debug source set** 的 `app/src/debug/assets/profile/43499/debug-a301so.conf`；`FinishHandoffAdapter` 只接收值对象，不读文件（分层修正 I2）。
- debug 资产迁移 `main/assets → app/src/debug/assets`；release 构建不存在该文件即 fail-closed（I3）。

## 4. 机制修复与保留项

- D6 reclaim 同 fd 设 `SO_SNDBUF/O_NONBLOCK` 并发送（验收：1MiB + skb 形状字段日志）。
- D3 ghost disarm 参数化（见 §2）；D5 arm_sequence/arm_hold 生效；`requeue_expected` 生效并写测试。
- pselect/TCP 仅参数化；debug 注入只启用 multicast（D9/D10/D11 后置）。
- 本批不实现 compact nice 轮转（D5 剩余项）：登记技术债，P0b generator/debug 注入前决定；如失败即停。

## 5. 验证（可执行；host 使用系统 clang/cmake）

1. ✅ Native：`:app:externalNativeBuildCleanDebug` + `:app:externalNativeBuildDebug` → EXIT=0，warning/error grep=0（15 TU 全量重建，2026-10-27）。
2. ⏳ host 参数测试：**未交付**（validate.c/host harness/Gradle `hostValidation43499` 未建；校验以 core 内联静态函数实现）。登记债 §7；偿还=P0b 前。
3. ✅ Kotlin：`:app:compileDebugKotlin` EXIT=0；`:app:testDebugUnitTest --tests "com.ghostlock.app.backend.cve43499.*" --tests "com.ghostlock.app.core.flow.*"` → 17 tests / 0 failures / 0 errors（XML 全量计数）。
4. ✅ 扫描脚本：`python3 tools/scan_native_literals.py --gate r2` → **exit 0**，`literals=1337 allowed=1337 unclassified=0 target=0`，residue `PHYS_/STRUCT_/WIRE_/reserved` 全 0；新增 magic 逃逸闭表。
5. ⏳ 真机门禁：**本批缺席（用户知情批准）**；P0c 冷机 A301SO multicast 单 primitive 门禁 + device-gates 记录。技术债登记见 §7。
6. ✅ ABI 布局：`tools/abi_layout_probe.c` host 实跑，20 个 sizeof 与 [abi](43499-r1-freeze-abi.md) §3 after 矩阵全等；NDK 每头 `_Static_assert` 强制。
7. ⚠️ `:app:assembleRelease`：首次失败于 stale build 目录重复 class（`BackendRunDispatcher 2.class`，与 R2 改动无关）；已 `./gradlew clean` 重跑，结果见 R2c 报告。
8. ✅ **R3 三路（2026-10-27）**：R3a Native（三 carrier/actor/page/placement + flow seam）+ R3b Kotlin
   三 generator/debug 配置/RequestFactory；R3b 单测 30/0/0（含三路字节向量对拍 legacy 常量）；
   `tcp_zerocopy_spec` 加双 fill（send 0x33/mapping 0x55）⇒ sizeof tcp=176、primitive_spec=192；
   scanner `--gate r2` exit 0（allowlist 重签 sha256 `07c8cad4…`）。真机门禁缺失（A301SO 离线）
   ⇒ 三路 UNSUPPORTED/待 P0c，不做 fallback。

9. ✅ **R3d 多写链（2026-10-27，writer r3d-impl）**：Kotlin 数据平面 + Flow 编排实现
   （W1→W1b→W2(+fast-repair)→W3；每次写=新 placement→landing→actor 事务；pselect 重喷=新事务）；
   Credential 布局替换具名拒绝；`Cve43499Lifecycle` 扩多链 + `closeChain`；target/verifier/restarter
   seam 全部 Kotlin。**Native 零改动**（ABI v4 字段/尺寸/断言不变，scanner r2 仍 exit 0；
   Native `.c/.h` mtime 早于本批开工）。门禁：compileDebugKotlin EXIT=0、过滤单测 48/0/0、
   NDK clean+debug EXIT=0 零告警、scanner r2 EXIT=0、git diff --check EXIT=0。
   **P0c 前置阻断**：A) victim task_struct 地址（需 Native perf 泄漏机制，本批禁止）；
   B) victim 协议探针/park/重生成（'M'/'F'/'P'，victim/ 冻结）⇒ W2/W3 真机物理目标不可得，
   默认路径保持单 W1；详见 review §6.4。

10. ✅ **R3f（task-45，2026-10-27）**：用户指令「finish 脚本要交给 victim 执行，把 W1 的墙拆掉允许
    进入后续步骤」。纯 Kotlin：victim 行协议（probe/finish/park/bye + victim 内 fork/exec 脚本 +
    watchdog + exit/输出 SHA-256）、`ProcessVictimSession.finish/rooted/detachRooted`、Flow finish
    优先 rooted victim（脚本 SHA 对拍；victim 失败/超时 fail-closed 不回退 su；无 rooted victim 时
    App+su fallback 的“成功”结果一律拒绝）、`Cve43499Lifecycle.closeAll` rooted 只 detach
    （evidence `retained=rooted-child-parked`）；W1 `UNSUPPORTED`/耗尽 `UNVERIFIED` 非阻断
    （`w1Verify=unsupported|unverified`+reason，保留 W1 链/facts 继续 W1b/W2/W3；RouteMiss/Failed
    不变）。门禁：compileDebugKotlin EXIT=0、过滤单测 XML **109/0/0**、`git diff --check` EXIT=0；
    Native 零改动。真机未验证（P0c）。详见 review §7。

11. ✅ **R3f/task-46 perf 审计修复（2026-10-27）**：`PERF_SAMPLE_REGS_INTR` 由 `1<<14`
    (WEIGHT) 修为 `1<<18`（debug `perf.attr.sample_type=262145`、CLOEXEC、`enabled=0`）；
    TargetSource expected 派生；B5–B9 Native 机械回执/资源记账（close/open 清理、JNI handle、
    owner 结算、`closeRetained` 透传）；10-08 finding 撤回“寄存器不投递”。门禁：compile EXIT=0、
    过滤单测 XML 109/0/0、NDK 0 warning、scanner r2 EXIT=0（allowlist `7724a69b…`）、ABI probe
    128/32/48/80、diff-check EXIT=0。**仍 UNVERIFIED**：G1/262145 寄存器投递、perf_event_paranoid。
    详见 review §7.4。

## 6. 文档同步

- `abi.h` 版本注释 → ABI v4；同步 `43499-c11-header-contract-plan.md`、`43499-43284-jni-rewrite-plan.md`、`43499-resource-primitives-implementation-plan.md`（G4L）中的“ABI v3”表述；UML 结构/API 图同批更新（AGENTS §9）。
- approval-status §6.5 登记本计划与豁免；旧参数化计划 43499 段标“已被 R30 取代”。

## 7. 技术债（登记 approval-status）

| 债 | 代价 | 偿还 |
|---|---|---|
| 真机门禁缺席（攻击路径改动先行） | 命中/写/uid 未证 | P0c 冷机 A301SO multicast 门禁 |
| host C harness **未建**（本批简化） | V1–V12 参数校验负例未可执行化（仅内联 helper + 审读） | **P0b 前补** `validate.c`+`host/validation_host.c`+Gradle `hostValidation43499`；owner=Lead |
| pselect/TCP 仅参数化未验 | 未启用，风险低 | 启用前单独批 |
| debug loader/资产为临时物 | 发布隔离需持续检查 | P0b 后清理 debug 字段、release 断言 |
| compact nice 轮转未实现 | PI race 形状差异 | P0b generator 批前评估 |
| Kotlin debug 注入参数仍为 `A301SoDebugConfig.executionParameters()` 显式常量（非 Native 默认值） | 与“全部参数化”只差 debug 注入策略层的配置化 | P0c generator/真机调参批迁入 debug conf |
| ~~R3d：W1b/W2/W3 多写 + fast-repair + Credential generator~~ | **已实现（Kotlin，工作树未 commit）**：多写链 Flow + generator + fast-repair 语义 + Credential 布局 | 真机仍 UNVERIFIED（P0c） |
| ~~pselect 每 attempt 重喷页~~ | **已实现（Flow 重试=新事务/新页/新帧）**；carrier 内 per-attempt 重建 fd_set 保留 | 真机 retry 有效性未证（P0c） |
| **P0c 前置阻断 A：victim task_struct 地址原语** | **R3e 已实现（task-44）**：机制-only Native 3 入口（无状态）+ Kotlin 解析/2-of-3/门槛/W2 precheck；**R3f/task-46 修复**：REGS_INTR `1<<14`→`1<<18`（262145）、B5–B9 机械回执/记账、`closeRetained` 透传；**默认关闭 `perf.enabled=0`** | G1 真机门禁（A301SO 冷机、262145 寄存器投递判据 280B/ABI=1/32 regs）为生产启用硬门；技术债 owner=Lead，偿还=P0c |
| **P0c 前置阻断 B：victim 协议探针/park/重生成缺失** | W3 leaf 方向/seccomp 探针、链轮次>1 的 rooted child 重生成仍缺失；默认 verifier UNSUPPORTED。**rooted child park 已由 R3f 实现**（`ProcessVictim` park + `finish` 协议 + `detachRooted`；见 review §7） | 剩余需 victim/ 改造（'M'/'F' 探针 + per-victim task 泄漏 + 重生成协议）；P0c 批 |
| **R3f：parked rooted child/管道保留（有意）** | `closeAll` 对 rooted 只 `detachRooted()`（park），不 close 流/不 destroy；root child 与 2 条管道 fd 驻留到 App 进程退出；无回收/复检 | P0c 观察驻留副作用（owner=Lead）；若需要回收策略另批设计 |
| **R3f：finish watchdog 只杀 direct child** | victim 内 `kill -9 <script pid>` 不保证级联回收脚本孙进程；hung 场景以超时+exit 证据 fail-closed | P0c 真机检查残留进程；需要时改用进程组（须重新设计协议） |
| **R3f：无 rooted victim 时 su fallback 成功被拒** | `finishExecuteStep` 对 App+su fallback 的 exit==0/未超时结果返回 `su-fallback-refused`（防静默 su 成功）；单 W1 路径的行为变化 | 若未来允许单 W1 finish，须用户明确批准并给非 su 的 rooted 执行方式 |
| fast-repair 无 prebuild 页复用 | 每次修复=新 placement（无 legacy 免 spray 收益）；真机修复有效性未证 | P0c 真机对照；若失败⇒在 Native 机制边界内评估 stash/quarantine（须用户批准） |
| host C harness 仍未建（`gl43499/host/` 空；R3b 用 Kotlin 纯函数向量替代） | Native 校验负例不可执行化 | P0c 前（owner=Lead） |
| 三路真机门禁缺席（A301SO 不在线） | 命中/写/uid 未证 | P0c 冷机单 route 门禁；未过门禁路线标 UNSUPPORTED |

## 8. 批次

- **R0** 本计划批准（design-critic 阻断 0 + 用户在 approval-status §6.5）
- **R1** ✅ ABI v4：删 reserved + struct_size/version + 字面量闭表 + C 参数化（含 D3/D5；D6 基线已有并复核）
- **R2** ✅ Kotlin DTO/去默认值/data loader/debug asset 迁移 + 对拍测试（2026-10-27 已实现，未 commit）
- **R3** 验证（§5，host harness 债）→ **R4** P0b multicast generator（stamp/三地址/landing bytes/页接受）→ **P0c** 真机门禁
- **R2 执行记录（2026-10-27，writer impl-43499）**：R2a→R2b→R2c 顺序执行；NDK/scanner/compile/单测门禁全绿；剩余 UNVERIFIED=host harness、真机门禁（P0c）、assembleRelease 清理后重跑、debug 注入参数配置化。
- **R3 执行记录（2026-10-27）**：用户要求三路同批。R3a（writer routes-impl2）= Native 三路修复 + placement facts→generator seam + 页接受/回执/时序；R3b/R3c（writer routes-impl3）= 三 generator + debug 配置三段 + RequestFactory 放开三路 + 单测 + 门禁/矩阵回填。ABI：tcp_spec 176、primitive_spec 192。真机门禁 UNSUPPORTED/待 P0c。剩余：host C harness。
- **R3d 执行记录（2026-10-27，writer r3d-impl）**：按用户硬约束“Native 零逻辑/零状态机”完成 Kotlin 数据平面与 Flow 编排：
  Credential 布局（0x1080/0x6800 cred 镜像 + init_cred 写值）、`Cve43499DebugWritePlanner` 目标派生、
  `Cve43499Lifecycle` 多链 + `closeChain`、Flow 多写链（W1/W1b/W2+fast-repair/W3、attempts、pselect 新事务重喷）、
  target/verifier/restarter seam；单测 48/0/0、NDK 零告警、scanner r2 exit 0。**Native ABI diff=0**。
  剩余=P0c 前置阻断 A（victim task 泄漏原语）/B（victim 协议探针/park/重生成）+ 真机三路门禁。

## 9. 回滚/明确保留

- 回滚：R1–R2 仅工作树改动（不 commit 前可回退）；恢复 HEAD 基线 + 重建 native；ABI v4 未随版本发布。
- 保留：JNI 回执三态语义、Flow 10 步顺序、lifecycle 所有权/close 链、legacy/very_legacy 只读参考、pselect/TCP 不启用。

## 10. R0/R1 批准与冻结（已批准：方案 A + ABI v4 删 reserved + A 全参数化）

- **已批准**（用户 2026-10-27「全部批准」「A」）：R0=方向/范围/批次/真机门禁缺席豁免；ABI v4=删除全部 reserved（含 abi.h/result.h/输出结构）+ struct_size/abi_version 严格校验；**A 决策**=`GLK43499_STRUCT_*`（6 容量）与 `GLK43499_WIRE_*`（2 LEAK_TAG）也全部参数化（Native 不保留任何目标/容量/协议数值宏）。
- **R1 冻结交付物**（本修订，待第四次只读复核）：[inventory](43499-r1-freeze-inventory.md)（索引/摘要）+ 附件 [fields](43499-r1-freeze-fields.md)（R2 最终逐字段）、[abi](43499-r1-freeze-abi.md)（reserved/矩阵/单源/static_assert）、[validation](43499-r1-freeze-validation.md)（校验矩阵/A 迁移）+ `tools/scan_native_literals.py`（R1/R2 双 gate）。
- **R2 冻结文件清单（逐文件）**：`core/{abi.h,result.h,placement.h,page.h,actor.h,primitive.h,kernel_internal.h,validate_internal.h(NEW),validate.c(NEW),placement.c,page.c,page_internal.h,actor.c,carrier_internal.h,carrier_multicast.c,carrier_pselect.c,carrier_tcp.c}`、`jni43499.c`、`jni/Native43499Jni.kt`、`backend/cve43499/{debug,owned,lifecycle,flow,finish}`、`app/build.gradle.kts`、`app/src/debug/assets/profile/43499/debug-a301so.conf`、`gl43499/host/CMakeLists.txt(NEW)`、`gl43499/host/validation_host.c(NEW)`、`tools/abi_layout_probe.c(NEW)`。
- **R2 门禁**：`--gate r2` exit 0（residue 全 0）+ NDK/compile/assembleRelease 绿 + host harness 绿 + 过滤单测绿；真机门禁延至 P0c（tech-debt owner=Lead）。
- 半成品处置：R2 从 HEAD `a9af5482` 重建（或隔离 worktree），P0a/P0b 半成品丢弃；基线 hash 表与命令见 [inventory](43499-r1-freeze-inventory.md) §5。

- **R3f 执行记录（task-45，2026-10-27，writer routes-impl3）**：用户指令「finish 脚本要交给 victim
  执行，把 W1 的墙拆掉允许进入后续步骤」。纯 Kotlin 实现：
  1) victim 固定 shell 行协议（`probe`/`finish`/`park`/`bye`；finish 在 victim 内 fork/exec 脚本 +
     watchdog 有界 kill + `GLK_FINISH_TIMEOUT:1`/`GLK_FINISH_EXIT:<code>` + 输出 SHA-256）；
  2) session `rooted`（uid==0 置位）/`finish(scriptPath,timeoutMs)`/`detachRooted()`；
  3) Flow finish 优先 rooted victim（脚本 SHA 对拍、victim 失败/超时 fail-closed 不回退 su；
     无 rooted victim 时 su fallback 成功结果一律拒绝）；rooted victim 在 `closeAll` 只 detach
     （evidence `retained=rooted-child-parked`，不 kill）；
  4) W1 墙拆除：`UNSUPPORTED`/耗尽 `UNVERIFIED` 非阻断并保留链/facts 继续 W1b/W2/W3，
     evidence `w1Verify(+Reason)`；RouteMiss/Failed 语义不变。
  门禁：compileDebugKotlin EXIT=0、过滤单测 XML 109/0/0、git diff --check EXIT=0；Native 零改动。
  真机/驻留副作用未验证（P0c；见 review §7.3）。

- **R3e/task-46 perf R3f 记录（2026-10-27，writer r3e-impl；Native 仅机械字段/回执）**：
  B1 `PERF_SAMPLE_REGS_INTR` 由误写 `1<<14`(WEIGHT) 改为 `1<<18`（单一常量+注释禁写；debug
  `sample_type=262145`、`attr.flags=8`/CLOEXEC、`enabled=0`；回归「仅 1<<14⇒perf-config-invalid」；
  10-08 finding 撤回、原始输出保留）；B5–B9 close/open 清理记账、JNI handle 关闭顺序、owner 仅
  非 NOT_ACCEPTED 清 token、`Failed/Missing.closeRetained` 由 Flow W2 失败 evidence 透传
  （`perfCloseRetained`；本 writer 已核对保留，未回退）。建议：`ring_pages=2` 幂、tagShift=0⇒nibble=0。
  门禁（18:33:33）：compile EXIT=0；过滤单测 XML 109/0/0；NDK CleanDebug+Debug EXIT=0/0 warning；
  scanner r2 EXIT=0（1574 literals、allowlist `7724a69b…`）；ABI probe 128/32/48/80 + 既有 20 项不变；
  `git diff --check` EXIT=0。资产：debug conf `38c5fc8b…`、finding `c362fb8b…`。
  **UNVERIFIED**：G1/262145 寄存器投递、perf_event_paranoid；`perf.enabled=0`，未 commit/push。

- **R4/批 B1+B2：通用 victim 接入（2026-10-27，writer routes-impl3；Native 仍纯机械，B2 零 Native 改动）**：
  B1（task-53，Lead 复核转交）：`core/victim/VictimNativeJni` 1:1 映射机械 API + Native 9 个通用
  JNI 符号；门禁 compile EXIT=0、过滤单测 151/0/0、scanner r2 EXIT=0、ABI probe 绿。B2（task-54）：
  `Cve43499JniVictimRuntime` + `Cve43499VictimAdapter`（通用 victim→backend `VictimSession`：
  start/uid/rooted/identity/finish(execScript)/detachRooted/close）+ `Cve43499JniVictimTargetSource`
  （helper self-perf→raw→Kotlin 解析/投票/2-of-3；ABI=2 接受；W1 permissive 未确认⇒
  `perf-permissive-required`；helper errno/截断 fail-closed）；debug `injection.victim=process|
  jni-spawn`（默认 process；jni-spawn 缺 perf 配置⇒RequestFactory Refused，不静默回退）；Flow
  `Flow43499RunSpec.victimFactory` + spec 级 lifecycle 工厂（保留单 lambda 兼容构造）；链式顺序
  W1→victim.start→W1b→W2→victim.probe→W3 不变；W2 baseline/precheck 用新 identity；finish 由
  rooted victim execScript 执行；rooted detach/park 不 kill；**单 W1 路径 10 步顺序回归固化**。
  门禁：compile/UTest EXIT=0；过滤单测 EXIT=0、XML **169/0/0**（B2 新增 18）；NDK CleanDebug+
  Debug EXIT=0/0 warning（Native 零改动）；scanner r2 EXIT=0（literals=2113、allowlist
  `02bddb0540…`）；ABI probe 24 sizeof 不变；`git diff --check` EXIT=0；资产 `debug-a301so.conf`
  sha `280c9128bd26…`。**UNVERIFIED**：E4/E2/E5 真机 helper exec/uid/seccomp/perf/寄存器投递、
  permissive 时序、finish 输出语义对拍、端到端真机接线；`perf.enabled=0`，未 commit/push。

10. ✅ **mondrian 5.10 配置 + placement floor（2026-10-28，task-58，工作树未 commit）**：
    - 配置 `debug-mondrian.conf`（8,573 B，full.conf + 5.10 multicast/select probe + legacy2 常量，逐字段来源）；
      loader 设备选择（A301SO 默认不变、mondrian 新增、未知/歧义 fail-closed、release 校验）。
    - placement floor：5.10 非整除 `mm_struct=960` / `allocation=32768` 允许，`objects_per_slab=allocation/mm`
      （legacy2 `support.cpp:564`；A301SO 1024/32768=32 数值不变、mondrian=34 余 128）；mm=0/mm>allocation/
      结果 0/>UINT32_MAX 拒绝；派生式收敛到 `placement.h` inline 助手。
    - 单测：Native host `tools/tests/placement_floor_test.c` **EXIT=0**（A301SO=32/mondrian=34/边界/溢出）；
      Kotlin `MondrianDebugConfigTest` **6/0/0**（conf 字段/设备选择/release/floor 边界/RequestFactory）。
    - 门禁：compile EXIT=0；过滤单测 **178/0/0**；NDK CleanDebug+Debug EXIT=0/0 warning；scanner r2 EXIT=0
      （literals=2115、allowlist 重签 `f7f8d461…`）；ABI probe EXIT=0（placement_request=216 不变）；
      `git diff --check` EXIT=0；`assembleDebug` EXIT=0。完整 `hostValidation43499` 仍未交付（本项只是 floor
      窄 host probe；原 §5.2 债不变）。
    - **UNVERIFIED**：设备离线（`adb devices` 空）⇒ E0/E4/E1 未执行；mondrian release/offset 真机对拍、
      permissive/W1 门禁待设备恢复。`perf.enabled=0`，不 commit/push。

- **P1a parity：G02/G06/G07/G09/N02/N03（2026-10-28，writer routes-impl3；设备离线，纯代码+单测）**：
  **G02** `carrier_pselect.c` 每 attempt 超时=`min(remaining, timeout_ns/max(1,budget))`
  （800ms/4=200ms，等价 legacy2 route_select.cpp:457-470；新纯头 `pselect_timeout_internal.h`；
  不改 ABI；host 单测 tools/tests/pselect_timeout_test.c EXIT=0）；**G06** JNI
  `Native43499Jni.raiseProcessLimits`（NOFILE/NPROC soft→hard，before/after/errno 回执）+
  `owned/ProcessLimits` + Flow validate preflight（失败 `process-limits:*` fail-closed、成功 evidence）；
  **G07** `owned/KernelsuGuard`（模块/sysfs/路径三类 positive signal；Loaded ⇒ W1 前
  `ksu-loaded` 拒绝 + evidence；unreadable 不误判）；**G09** `page.c` 两处 sendmsg +
  `carrier_tcp.c` send 加 `MSG_NOSIGNAL`；**N02** spawn file_actions 冻结（addclose 先于
  adddup2，只 close 原 fd>2 且 !=3/4，`glk_victim_should_close_fd`；host 规则单测
  tools/tests/victim_fd_rules_test.c EXIT=0）；**N03** `victim_helper.c` ring 拷贝 acquire
  load+fence、tail release store（对齐 perf.c:330-333）。门禁：compile/UTest EXIT=0；过滤单测
  EXIT=0、XML **195/0/0**；NDK CleanDebug+Debug EXIT=0/0 warning、Release EXIT=0（compiler
  warning=0；CXX5106 AGP 环境告警 13 条）；scanner r2 EXIT=0（files=25、literals=2155、
  allowlist `3af0bd65…`）；ABI probe 24 sizeof 不变；verify_jni_exports Debug/Release
  expected=25 found=25 EXIT=0；verify_no_fork_imports EXIT=0；git diff --check EXIT=0。
  **UNVERIFIED**：设备离线，G02/G06/G07/N02/N03 真机行为未验证；G01/G05 未做；
  `perf.enabled=0`；未 commit/push。

- **P1b-1：G01 fail-stop + G08 CPU pin（2026-10-28，writer routes-impl3；设备离线）**：
  **G01** `owned/FailStopController`（可注入 terminator/evidenceSink/logger；evidence→logger→
  terminate 顺序；只一次；`fail-stop:<stage>:<reason>`；evidence 字段 4 个）；Flow 在
  W1/W1b/W2/W2-repair/W3/单 W1 actor dirty、close-chain/lifecycle.close/cleanup 非 parked
  retained 触发；Actor `retainedForProcessExit` 归类 dirty；两份 debug conf `failStop.enabled=1`；
  RequestFactory sink=logcat+filesDir/fail-stop.log append/flush/fsync。**G08** 不改 ABI：
  `core/cpu_pin_internal.h` u32 全 1 no-pin 哨兵（Kotlin -1 经 JNI get_int 同值）；actor.c
  owner pin/waiter arm 按哨兵跳过、consumer 恒 pin；校验 owner/waiter 允许哨兵、consumer 必须
  有效 CPU；两份 conf `execution { owner_cpu=-1; waiter_cpu=-1; consumer_cpu=2 }`，loader 解析并
  覆盖 executionParameters。门禁：compile EXIT=0；过滤单测 EXIT=0、XML **205/0/0**（新增 10 UI +
  host cpu_pin 1）；NDK CleanDebug+Debug EXIT=0/0 warning、Release EXIT=0（compiler warning=0）；
  scanner r2 EXIT=0（files=26、literals=2160、allowlist `f1489bfd…`）；ABI probe 24 sizeof 不变；
  verify_jni_exports expected=25 found=25；verify_no_fork_imports EXIT=0；git diff --check EXIT=0。
  **UNVERIFIED**：设备离线，G01 真机终止/落盘时序与 G08 真机 pin 物理效果未验证；
  「首次真机门禁只 pin consumer，后续单变量 A/B」仅登记；`perf.enabled=0`；未 commit/push。

- **A1 finish victim-only（2026-10-28，writer routes-impl3；用户令「先编码不编译」）**：
  Flow `finishExecuteStep` 唯一执行点=rooted victim（VictimFinish/execScript，保留脚本 SHA 对拍与
  exit/timedOut/output SHA/markers）；无 rooted victim 时**删除 App/su 执行路径**（不调用
  `finishExecutor`/FinishExecution/`su -c`/ProcessBuilder），直接 Refused
  `cve43499:finish.execute:su-fallback-refused`；删除 `finishExecuteViaAppFallback`/
  `emptyExecutionEvidence`/`FINISH_SOURCE_APP_FALLBACK`；`Flow43499RunSpec.finishExecutor` @Deprecated
  保留字段；`FinishHandoffAdapter` 删除 `execute()`/`executor()`、不再传 executor；`FinishExecution`
  @Deprecated（无生产调用）；新增 `tools/verify_43499_finish_victim_only.py` 静态 gate（剥注释后禁
  ProcessBuilder( / Runtime.exec( / "su -c" / FinishExecution(；要求 victimFinish 路径与
  su-fallback-refused；豁免清单 3 项带理由；`--self-test` 5 违规样本 + 临时文件 + 3 干净样本）。
  静态实跑：gate EXIT=0（files=55、exempt=3、violations=0）；self-test EXIT=0。测试先写/更新
  （无 rooted ⇒ Refused+executor 计数 0；rooted ⇒ VictimFinish；单 W1 拒绝；adapter 无构造）但
  **按用户令未运行 compile/单测/NDK**；不得宣称门禁绿。UNVERIFIED：编译正确性/测试结果、
  G01/G08 真机行为、G05 v2 排期。未 commit、`perf.enabled=0`、未碰 98260。

- **G05 fast-repair 预建实现（2026-10-28，writer routes-impl3；按用户令先编码不编译）**：
  `owned/Cve43499PrebuiltRepair` 状态机（最多 1、不入 active chains、clean/dirty discard 回执）；Lifecycle
  prebuilt API + closeAll 顺序 active→prebuilt→victim；Runner prebuild/minimal-transaction（activation 后只
  prepareActor/executeActor/clean close + `Cve43499RepairTiming`）；Flow W1 出口预建（Lead delta：W1 dirty
  不进 W1b/W2；与 v2 Q1=B 字面差异登记）、默认开关 false 零行为变化、allowFullFallback=false、B1
  `requireCleanCloseAfterCred` 覆盖 7 类 close + retained⇒W2 token/G01 fail-stop（failStop=0 仍 Failed）+
  W3/finish 前 post-cred preflight + closeAll 晚 retained 使 acceptance 不可达；两份 conf writeChain 开关；
  evidence w2RepairMode/prebuildUs/w2RepairUs/子项/prebuiltState。测试先写（状态机/预建成功/W2 链与
  repair actor close-retained/dirty prebuild/allowFullFallback 两态/关闭旧路径）。**未跑 compile/单测/NDK**；
  编译/测试/真机 UNVERIFIED；不 commit、perf.enabled=0。

- **A1 delta + A2 gate（2026-10-28，writer routes-impl3；provisional-baseline=HEAD `a9af5482…`/status `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`；98260 R1a/R1e 冻结后重采重跑）**：
  A1 delta：finish 超时只 detach/park（不 close/kill/destroy），`VictimFinishResult.Failed.retained=true`，
  Flow evidence `victimRetained`；仅 closeAll（非 rooted）允许终止。A2 gate：FinishExecution 整类豁免（无生产
  调用点为 PASS）、ProcessVictimSession.finish 调用点白名单+timeout 块禁 closeLocked/kill/destroy、7 类 close
  合同断言、负例（finish ProcessBuilder/timeout kill/生产 FinishExecution 引用）与可失败 self-test。按用户令
  未跑 compile/单测/NDK；编译/测试/真机 UNVERIFIED；不 commit、perf.enabled=0。

- **G05 Q1=B 修正（2026-10-28，writer routes-impl3；provisional-baseline=HEAD `a9af5482…`/status
  `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：`prebuildRepair` 移至
  **W1b 成功出口、W2 之前**（skip/repaired 两出口），W1 出口恢复不预建；W1 dirty 走既有失败/G01，
  W1b dirty ⇒ 不预建/不进 W2，clean failure ⇒ W1b 继续（allowFullFallback 语义不变）；token 前缀
  `cve43499:write.w1b:fast-repair-prebuild-retained:*`；测试先写（顺序 W1→W1b→prebuild→W2、W1b dirty
  不预建/不进 W2、dirty 期望改 W1b）。按用户令未跑 compile/单测/NDK；静态检查另报；UNVERIFIED：
  编译/测试/真机、provisional 基线重采；不 commit、perf.enabled=0。
- **P2 JNI L1–L4 边界修复（2026-10-28，task-74；工作树未 commit；provisional-baseline=HEAD `a9af5482…`/status `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：
  L1 `primitiveKind` 显式字段（三 @Keep 实现+companion 常量；generator/tests 构造同步），JNI 删除 primitive
  类名 IsInstanceOf；L2 翻译失败 NOT_ACCEPTED 且不写回执（仅 core 后写结果）；L3 空数组合法/OOM=SYSTEM_ERROR+
  ENOMEM/非法=INVALID_ARGUMENT/读取异常=TRANSLATION_FAILED；L4 perf collect 按 core `bytes_copied` 校验≤capacity
  后只拷该长度（去 data_size 全量拷贝，capacity<data_size 保持具名失败）。测试先写 `JniBoundaryContractTest`
  （反射+host-parseable 源码契约）。**按用户令未跑 compile/单测/NDK**；静态 `git diff --check` EXIT=0；源码
  JNI 导出期望/定义 25/25（task-63 G06 已增 1，本批无新增）；`verify_jni_exports.py` 因 build/cxx 产物缺失
  EXIT=2 未运行；代码/测试编译与真机 UNVERIFIED；`perf.enabled=0`；不 commit。
- **P3 Native/JNI 机械卫生 B11–B17 + JNI L5–L8（2026-10-28，task-77；工作树未 commit；provisional-baseline=HEAD `a9af5482…`/status `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：
  B11 pthread_create 取返回值（actor 3/tcp 1）；B12 multicast trigger 删 loop 内 deadline/expired（不依赖 vDSO）；
  B13 inflight 有界 spin（超限 EBUSY/STEP_INFLIGHT）；B15 placement scan_round_limit≤UINT32_MAX、三 carrier
  operation_budget≤INT32_MAX；B16 pselect retained 仅资源实际未清时=1、clean 显式 0；B17 page abort 前写 reclaim
  fd 到 Page、abort close_pair 后同步未关 fd（retained Page 可关）。JNI L5 handle==0 前置、L6 jboolean close_
  operation+唯一 bool writer、L7 四哨兵 C `_Static_assert`+Kotlin 对拍、L8 priority int32/PRIO 域校验。测试
  先写 `NativeHygieneContractTest`（未运行）。静态 `git diff --check` EXIT=0；JNI 导出 25/25 无变化；
  `verify_jni_exports.py` 因 build/cxx 产物缺失 EXIT=2 未运行。**未跑 compile/单测/NDK**；编译/测试/真机
  UNVERIFIED；perf.enabled=0；不 commit。

- **G04 W3 硬门实现（2026-10-28，writer routes-impl3；provisional-baseline=HEAD `a9af5482…`/status
  `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：新增
  `backend/cve43499/w3/Cve43499W3HardGate`（P1–P8、双源 status、LeafDecision、gate-* reason、
  coldRebootRequired evidence）；task-45 append（capability bits 7/8/9、VictimProtocol
  status/comm-set/comm、ProcessVictim shell 固定 od 格式、ProcessVictimSession/VictimSession/
  Lifecycle append；既有 finish/park/bye/detach 语义不变）；Flow W3 preflight + finish 前置 gate
  （fail⇒write.w3:gate-* + G01 fail-stop + coldReboot evidence）；测试先写未运行。
  **DEFERRED**：helper Native append（victim_protocol.h/c、victim_helper.c）因 in-progress task-56
  写范围冲突未做，待其收尾补；J/helper self-probe 缺失⇒FAIL 不 fallback。按用户令未跑
  compile/单测/NDK；编译/测试/真机/E7 UNVERIFIED；不 commit、perf.enabled=0。
- **G19 pselect fd install 窗口收窄（2026-10-28，task-79；工作树未 commit；provisional-baseline=HEAD `a9af5482…`/status `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：
  `carrier_pselect_prepare` 只做 frame 解码/目标收集/`F_DUPFD_CLOEXEC` 备份/pipe+timerfd，**不 dup2**；
  `trigger` 在 attempt/consumer 启动前同一 native 调用 install（dup2）→ selected=1，所有 attempt 结束
  立即 restore → selected=0；install 失败 `failure_errno`+step IO 具名并 rollback，rollback 失败
  retained=1；restore 失败 retained=1/result=0/outcome errno；dispose 仅 selected!=0 时 restore，否则
  close_backups，不重复 restore。测试先写 `PselectWindowContractTest`（未运行）。静态 `git diff --check`
  EXIT=0；**未跑 compile/单测/NDK**；ABI/请求/回执/超时/target 不变；真机 UNVERIFIED；perf.enabled=0；
  不 commit。

- **G04-Native append（2026-10-28，writer routes-impl3；provisional-baseline=HEAD `a9af5482…`/status
  `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：`victim_protocol.h`
  append opcode 6/7/8 + capability enum 0–9（Kotlin 镜像；7/8/9 append；bit7 去向）+ 4 KiB/4087/15B
  常量；`victim_helper.c` append PROBE_STATUS/COMM_SET/COMM（机制-only，无 fork/system/execve）；
  dispatch 追加，既有 1–5 不变；`victim_protocol.c` 未改；Kotlin `VictimProtocol`/`VictimSession`
  append probeStatus/probeCommSet/probeComm；新增 `tools/verify_43499_probe_protocol.py` 静态对拍 gate
  （EXIT=0/self-test EXIT=0）。测试先写未运行；按用户令未跑 compile/单测/NDK；编译/测试/真机
  UNVERIFIED；不 commit、perf.enabled=0。

- **Kotlin B4/B6（2026-10-28，writer routes-impl3；provisional-baseline=HEAD `a9af5482…`/status
  `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：B4 readiness/
  request fail-closed（`unsupported-single-w1:*`，不标 supported，单 W1 路径不变）；B6 finish 派发前
  有界 settle（默认 2000ms 可配）+ module/enforce 有界轮询 + evidence；acceptance 改为恢复运行前
  记录值（避免 Accepted 永远不可达）；debug conf `finish` 段 fail-closed 解析+透传。测试先写未运行；
  按用户令未跑 compile/单测/NDK；编译/测试/真机 UNVERIFIED；不 commit、perf.enabled=0。
- **B7 KernelSnitch scan bounds fail-closed（2026-10-28，task-81；工作树未 commit；provisional-baseline=HEAD `a9af5482…`/status `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：
  新增 43499 专属 `core/kernelsnitch_bounds_internal.h`，从 detector `IDENTITY_START/END`/`COARSE_SZ`/`PAGE_SIZE`
  单源派生 identity/scan/step 常量与 token `scan-bounds-mismatch`（不改共享 glkgeneral 头）；placement
  validate 对 `identity_start/end`、`scan_lower/upper_bound`、`scan_step` 严格相等，不一致返回
  `GLK43499_PLACEMENT_VALIDATE_SCAN_BOUNDS_MISMATCH`→`INVALID_ARGUMENT`/`system_error=EINVAL`；A301SO/mondrian
  executionParameters 与 detector 常量一致。测试先写 `KernelsnitchBoundsContractTest`（未运行）。静态
  `git diff --check` EXIT=0；**未跑 compile/单测/NDK**；ABI/请求/回执结构不变；真机 UNVERIFIED；perf.enabled=0；
  不 commit。

- **Kotlin 卫生清理（2026-10-28，writer routes-impl3；provisional-baseline=HEAD `a9af5482…`/status
  `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：8 项卫生修复——
  w1SettleUs 接线、debugSession 改用 debug 执行参数、expectedUid 可配+fail-closed、resourcesReleased
  只降不升、identity 只读幂等（/proc status，不再 probe）、startVictim 幂等、ProcessVictim.close
  @Synchronized、死代码按禁用优先保留并登记。新增 `tools/verify_43499_kotlin_hygiene.py` 静态 gate
  （8 项+负例）EXIT=0；测试先写未运行；按用户令未跑 compile/单测/NDK；编译/测试/真机 UNVERIFIED；
  不 commit、perf.enabled=0。

- **A1/A2a provisional：公共 victim opt-in（2026-10-28，writer routes-impl3；provisional-baseline=HEAD
  `a9af5482…`/status `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：
  `injection.victim=common` opt-in（默认 process 不变、未知 fail-closed、RunSpec 透传、旧路径保留）；
  新增 `Cve43499VictimLaunchSpecFactory`（符号 capability/空 argv-env/perf 派生 rawSize/既有 limits）
  与 `Cve43499CommonVictimRuntime`（公共 core VictimSession+VictimNativeJni，作为 common victimFactory）；
  Q5 边界：43499 不使用 98260 script 载体，静态 gate 断言无引用；hoist plan 由 task-83 写，按令不编辑
  （D0 部分解决、Q2/Q4 待回填）。测试先写未运行；按用户令未跑 compile/单测/NDK；编译/测试/真机
  UNVERIFIED；不 commit、perf.enabled=0。

- **A1 killPolicy 接入（2026-10-28，writer routes-impl3；provisional-baseline=HEAD `a9af5482…`/status
  `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：新增
  `Cve43499VictimKillPolicy`（killOnTimeout=false/rooted close⇒detach、非 rooted 可回收/
  detachOnTimeout=true），factory create/policyOf 携带、CommonRuntime 透传 adapter、adapter close
  按 rooted 分流 detachRooted/park、finish 超时保留 victim；D0 前 backend 携带、D0 后映射公共 spec；
  旧路径不删不禁用。测试先写未运行；静态 gate 扩展 EXIT=0；按用户令未跑 compile/单测/NDK；
  编译/测试/真机 UNVERIFIED；不 commit、perf.enabled=0。

- **E1 landing clean-failure 诊断 + harness 重试（2026-10-28，writer routes-impl3；provisional-baseline=
  HEAD `a9af5482…`/status `15225492…`/r1a `e0969163…`/r1e `3ce0b807…`/98260 plan `bac0bfc0…`）**：
  E1 二次诊断 root cause=core page.c 页接受判据（预期可重试 clean failure，token 恢复/未写/无 panic）；
  LandingOutcome.CleanFailure reason 透出 callStatus/systemError；payload 对拍与 legacy2 5.10 一致、
  full.conf legacy1 geometry 未消费 ⇒ 无证据化 conf 修改；W1OnlyProbeTest 对 clean-failure RouteMiss
  有界重试（debug w1Attempts=15、每 attempt closeChain/evidence、其它失败立即 FAIL、成功仍
  REQUIRED_COLD_REBOOT）；assembleDebug/assembleDebugAndroidTest EXIT=0。未安装/未跑真机/未跑单测；
  真机 UNVERIFIED；不 commit、perf.enabled=0。

- **E1 重试轮加固 + placement errno 透出（2026-10-28，writer routes-impl3）**：E1 重试轮
  attempt1/2=landing page-accept clean-failure（预期可重试）、attempt3=preparePlacement
  system-error/not-applied（124s，无残留）；修复 PlacementStep.Failed reason 追加 callStatus/errno、
  harness 每 attempt 新 lifecycle+runner/250ms settle/上限 8/仅 landing clean-failure 重试/逐 attempt
  phase+proc(fd) 证据；`:app:clean :app:assembleDebug` EXIT=0、`assembleDebugAndroidTest` EXIT=0；
  未安装/未跑真机/未跑单测；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **G08 NO_PIN 哨兵误拒修复（2026-10-28，writer routes-impl3）**：E1 加固轮 attempt2 landing 成功但
  prepare-actor `actor-cpu` 失败（105s）；root cause=PageOwner.precheckPreparation 误拒 NO_PIN_CPU=-1
  （owner/waiter 不 pin）；修复抽 precheckActorCpuPinning（owner/waiter 允许 -1、consumer≥0、其它负值
  fail-closed、reason 细分），不改 JNI；`assembleDebug`+`assembleDebugAndroidTest` EXIT=0；测试先写；
  未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **actor-execute clean-failure 诊断 + harness 重试扩展（2026-10-28，writer routes-impl3）**：E1 第五轮
  landing/prepare-actor PASS、actor-execute clean-failure；executeActor/prepareActor CleanFailure reason
  透出 JNI 回执字段；新增 `isRetryableCleanFailure` 纯函数（landing/actor-execute/prepare-actor clean）
  接入 E1 harness（上限 8/250ms/fresh lifecycle/phase）；`assembleDebug`+`assembleDebugAndroidTest`
  EXIT=0；测试先写；未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **EACCES placement 诊断 append（2026-10-28，writer routes-impl3）**：E1 attempt3 placement EACCES；
  Native append `diag_step/diag_syscall/diag_errno`（sizeof 96→112、assert/freeze 同步、
  frozen ABI 变更待 Lead 确认、probe 待重采）；失败点枚举标注；JNI/Kotlin DTO/reason 透出；
  EACCES 审计最可疑 proc mem open/pin CPU，选项 A–E 待 E1 证据；`:app:clean :app:assembleDebug`
  EXIT=0；未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。
- **日志接入 append（task-101，2026-10-28，writer routes-impl2）**：43499 生产路径接通用日志——progress append-only seam、Cve43499RunLog 双 sink/脱敏/≤512B、阶段行、JNI 边界 gl_log；gate `verify_43499_log_sites.py`/`verify_no_log_in_pi.py`（self-test 变异 FAIL→PASS）；`assembleDebug`+`assembleDebugAndroidTest` EXIT=0；真机 UNVERIFIED；未 commit、perf.enabled=0。

- **actor timeout 诊断 append（2026-10-28，writer routes-impl3，进行中）**：core actor_execute_result append
  diag_stage/requeue_ret/requeue_errno（阶段枚举；PI 窗口内仅回执字段）；placement failure diag 兜底；
  harness residual threads/fds/children；ABI 临时诊断扩展 Lead 批准（placement 96→112、actor 88→136，
  probe 重采、freeze 同步）；jni43499.c JNI 写回/Kotlin 透出待 task-101 完成后串行接；构建
  assembleDebug/assembleDebugAndroidTest EXIT=0；未安装/未跑真机；真机 UNVERIFIED。

- **actor timeout 阶段诊断透出（task-102 收尾，2026-10-28）**：jni43499.c 写回 actor
  diagStage/requeueRet/requeueErrno；Kotlin DTO/Lifecycle reason 透出；E1 harness residual
  threads/fds/children；ABI 临时扩展保留；assembleDebug/assembleDebugAndroidTest EXIT=0；
  未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **诊断字段生效修复（task-104，2026-10-28）**：placement publish_diag 全 return 复制 + 早失败
  VALIDATE/STAGING 标注；actor execute 阶段 diag_stage + requeue ret/errno（PI 窗口内仅回执字段）；
  clean assembleDebug/assembleDebugAndroidTest EXIT=0；未安装/未跑真机；真机 UNVERIFIED；
  不 commit、perf.enabled=0。

- **诊断 v3（task-105，2026-10-28）**：actor append fail_stage/publish_count/requeue_calls（首次失败冻结、
  sizeof 136→144、probe/freeze 同步）；placement 全 goto fail 标注；JNI/Kotlin 透出；构建
  assembleDebug/assembleDebugAndroidTest EXIT=0；未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **诊断 v4（task-107，2026-10-28）**：actor append diag_version marker + waiter_waiting/owner_started/
  requeue_expected 快照（144→160，probe/freeze 同步）；timeout fail_stage 无条件写；placement 独立枚举
  标注 goto fail；JNI/Kotlin 透出；根因假设=CMP_REQUEUE_PI 前 waiter 未排队（legacy2 显式等待）；
  双构建 EXIT=0；未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **actor timeout 证据 v5（task-108，2026-10-28）**：pi_shared/result append waiter futex ret/errno/in_futex/
  dispatched/target_value + 4 timestamps（160→216，probe/freeze 同步）；execute 有界预等 waiter/owner ready
  （诊断性）；marker=0x2026102805；JNI/Kotlin 透出；选项 A–D 待证据；双构建 EXIT=0；真机 UNVERIFIED；
  不 commit、perf.enabled=0。

- **placement leak 时序 + 独立枚举（task-109，2026-10-28）**：启用 MEASUREMENT_PIN/LEAK_CLONE/LEAK_OPEN/
  STOP/DRAIN；leak clone 后立即 open leak memfd（legacy2 对齐、A16 mm 引用尽早持有）；open 失败 child
  证据 append（placement_result 112→136，probe/freeze 同步）；JNI/Kotlin 透出；双构建 EXIT=0；
  未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **futex 实参诊断 v6（task-110，2026-10-28）**：append parked_timeout/op/val/val3/uaddr2/owner tid +
  时间戳（actor 216→288，probe/freeze 同步）；JNI/Kotlin 透出；下轮判定 timeout≈0 根因与 val3 选项；
  双构建 EXIT=0；未安装/未跑真机；真机 UNVERIFIED；不 commit、perf.enabled=0。

- **FUTEX_WAIT_REQUEUE_PI 立即 ETIMEDOUT（task-111，2026-10-28）**：legacy2 对拍发现 waiter timeout
  应为绝对 CLOCK_MONOTONIC deadline（race.cpp:171-189），本实现相对 {1,0} ⇒ 32µs ETIMEDOUT 根因假设；
  actor 改为 legacy 绝对 deadline（预算不变）；新增 debug-only 自测（Native+JNI+androidTest，只复刻
  legacy 同形一组，logcat GLK-FUTEX，不变体/不写内核）；双构建 EXIT=0；真机 UNVERIFIED；
  不 commit、perf.enabled=0。

- **waiter dispatch 无条件化（task-112，2026-10-28）**：legacy2 race.cpp:183-193 对齐——futex 返回后忽略
  wait_ret、dispatch 恒 1（保留 errno 诊断记录）；EDEADLK/超时/其它 errno 不阻止 dispatch；无 stop/cancel
  例外；双构建 EXIT=0、ABI 不变；真机 UNVERIFIED；不 commit、perf.enabled=0。
