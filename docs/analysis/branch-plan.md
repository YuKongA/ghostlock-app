# 分支总 plan（`vr-ko-bypass-dev`，2026-10-03）

> **唯一入口**：本文件回答「这个分支在做什么、现在到哪、下一步做什么、哪个文档算数」。
> 状态变化只改本文件的进度表；分册负责细节，不再各自维护全局顺序。
> 基线：`93afccc`。分支：`vr-ko-bypass-dev`。

## 1. 分支目标

把 native 攻击 runtime 重塑为「顶级组件架构 + 新 backend/terminal 可插拔」，且**不破坏 43499 攻击路径**；
在此过程中接入 **CVE-2026-43284** 作为首个非 43499 backend，验证 ADR-0004 的中性性。

三条并行工作流：

| 流 | 内容 | 权威文档 |
|---|---|---|
| **S1 框架收敛** | Phase 0/A/A2/A3/B/C：目录、契约、offset SSOT、platform/ancillary、去绑定 | `top-level-architecture-rewrite-plan.md` |
| **S2 Steps/Terminal 重设计** | Steps 可见、`Backend<StepSet>`、统一 terminal 接口、`ActivationContext` | `terminal-steps-redesign.md`（ADR-0004 R18–R21） |
| **S3 43284 backend** | 页缓存写 + LKM/UMH 终态、Kotlin 选择 | `cve-2026-43284-backend-plan.md` + 评估 |

## 2. 文档地图（现行 vs 历史）

**现行（权威）**

- 决策：`adr/0001`–`0004`（0004 含 R18–R21）。
- 执行/顺序：**本文件**。
- 分册：`top-level-architecture-rewrite-plan.md`（S1）、`terminal-steps-redesign.md`（S2）、
  `cve-2026-43284-backend-plan.md` + `cve-2026-43284-backend-assessment.md`（S3）。
- 闭包状态：`architecture-findings-register.md`。
- 审查原文（含被推翻）：`architecture-review-log.md`。
- 门禁证据：`device-gates/*.md` + `.native.log`。

**历史/分册参考（不作为顺序依据）**：`5x-tcp-geometry-plan`、`ancillary-*`、`extract-*`、`fallback-route-wire`、
`flexible-kernel-rw-primitive`、`gradle-test-decoupling`、`host-attack-dataflow`、`kernel-phys-offset`、
`placeholder-backend-survey`、`profile-editor-complete-fields`、`vr-guard-plan`、`exploit-session-generalization-plan`（Phase 0 已并入 S1）。

## 3. 总进度

**S1 框架收敛**
- [x] Phase 0：`CoreSession` 通用化；真机 `P0-01` PASS。
- [x] Phase A：A1 + 批 2 + 批 3a–3e（机械重排）；整体真机 `A3-01` PASS。
- [x] A2-1：环境探测 → `platform::runtime`。
- [x] A2-2a–e：attack 模块拆空（timer→support、bootstrap→backend、root script→terminal、primitive→backend）。
- [x] A2-3a：`in_direct_map` → `memory`，删除 `attack/`。
- [x] A2-3b：`AddressSpace` / `ResolvedAddresses` 拆分（布局不变）。
- [x] A2-3c：offset SSOT（ADR-0003 注册模型）。**已执行 -1/-2/-3**（提交 `d131197`/`6e6d155`/下一提交）：Document+Schema 影子 bind、43499 owner Schema 迁 parse、生产 strict + 三端 manifest（58/58 资产 strict 通过）；**-4（物理抽离）并入 A2-4**。**裁决**：-1..3 只做声明/校验所有权与 bind，不动 `kernel_offsets`/`TargetProfile` 物理布局（保 8 攻击函数 byte-identical）；物理拆分留 -4/A2-4。
- [x] A2-4/5 进行中（逐文件计划 `a2-4-5-plan.md`）：**A2-4-1**（`0f86b02` 中性 ancillary）、**A2-4-2**（`ab92439` 厂商下放 `platform::vivo`）、**A2-5-4**（`7f3d015` 删 `common.h`，新旧二进制 sha256 相同）、**A2-5-1/2**（`504583d` entry→中性 `Document`、owner 绑定移入 `backend_profile`；真机 `A2-5-1-20261004-…-pass.md` PASS）。
- [x] **A2-4-5**（`46224d8`）`AddressSpace` 解耦 `profile::TargetProfile`（新中性 POD `memory::LaunchGeometry`）；cmp 6/6 IDENTICAL；真机 `A2-4-5-…-pass.md` PASS。
- [x] A2-5-3（GLKv3 去 backend）随 A2-5-1/2 完成。
- [x] **A2-5-5**（`b5d7fb9`）include 防火墙（137→139 文件、白名单 18 条、stale 会 FAIL）+ fake backend（R8）；cmp 6/6 IDENTICAL。
- [x] **A2-4-3**（`9ceef1c`）`platform::abi` owner schema（31 key）+ `bind_all` 合并 shim；访问器去 backend（`active_profile()` 间接层）；白名单 18→16；真机 `A2-4-3-…-pass.md` PASS。
- [x] **身份词汇 → `contract`**（`4a06c66`）：`pipeline/{backend_contract,backend_policy,terminal_contract}` 删除，新增 `contract/identity.hpp`（kinds/availability/concepts/identities；中性终端接口词汇随迁并按 `using` 再导出）；防火墙白名单 **16→2**（仅 `support/util.cpp→backend`）；cmp 零影响；真机 `contract-identity-…-pass.md` PASS。
- [x] **A2-4-4（已裁决修正，`e549eeb`）**：字面「`kernel_offsets`/`TargetProfile` → backend」与 R1 冲突（`platform::abi`/`platform/vivo`/`memory` 都消费这两个类型，而 R1 禁止 `platform→backend`、`memory→backend`）。**裁决**：中性**数据词汇**迁 `contract`（`platform→contract`、`memory→contract` 合法），backend **策略**类型（`execution_settings`/`RouteKind`/`kRouteCatalog`）迁 `backend_profile`；`profile` 只留容器 framing/Document。实际实现以 `backend_profile/model.hpp` facade 再导出策略名（它们是冻结聚合的按值成员，物理迁 backend 会破 R1 或改 sizeof）。**二进制 sha256 与批前相同**；真机 `A2-4-4-…-pass.md` PASS。
- **A2-4 全部完成**（-1/-2/-3/-5 + -4 修正版）。
- [x] **A3-1**（`2e9bc58`）`contract::AddressDiscovery` + kernelsnitch 可选实现 + 契约测试；cmp 6/6 IDENTICAL；真机 `A3-1-…-pass.md` PASS；门禁记录 `6295dff`。
- [ ] **A3-2**：kernelsnitch provider → `backend/.../leak/`、`utils.h` 日志拆 `support`、喷雾尾部接线（依赖完整泄漏门禁）。
- [x] **B 契约/自动化同步**（本批）：`AGENTS.md`、`README.md`/`README_ZH.md`、`src/core/README.md`、
  `docs/development/adding-a-component.md` 已同步到当前 `Pipeline<Backend, Terminal>` /
  `pipeline/component_catalog.hpp` catalog / GLKv3 / R1 防火墙 / cmp 基线 `build/native/ghostlock-B0`；
  Kotlin/native/wire 对拍实跑：`make -C src native-host-tests`（`include_firewall_test`、`fake_backend_test`、
  `profile_manifest_test`、`profile_manifest_v3_test` 全绿）与 `./gradlew :app:testDebugUnitTest --offline`
  （`ProfileManifestAgreementTest`、`ProfileManifestV3AgreementTest`、`RouteCatalogAgreementTest` 通过）。
- [ ] **C**：`KernelMemory` 功能（另 PR）。

**CM 平台对策插件化（新增流，设计 `countermeasure-plugin-plan.md`）**
- [x] **CM-1**：ABI 头 `contract/glk_cm_abi.h`（自包含 C99，**全声明、最小实现**：阶段 5 个/触发器 4 个/能力 7 位全声明，宿主只实现 `ON_STAGE` + `{KERNEL_READ,KERNEL_WRITE,ALIAS,CHILD_TASK}`；预留项在加载期拒绝并记录）+ C++ 映射 `contract/countermeasure.hpp` + 契约测试（预留项若被标成实现即 FAIL）。host/NDK/lint/cmp 全绿（cmp 6/6 IDENTICAL）。
- [ ] **CM-2**：`platform::countermeasure::Loader`（白名单/哈希/dlopen/probe）+ 测试插件 `.so` + fail-closed 用例。
- [ ] **CM-3**：`ancillary::RuntimeRegistry` + Controller 分派（阶段/优先级/gate）+ 诊断；真机门禁（无插件时行为不变）。
- [ ] **CM-4**：在树对策（建议 `vr_task_tag`）写成参考插件验证语义一致 + ADR-0005（修订 R14）。
- [ ] **CM-5**：profile 加载清单/哈希字段 + Android assets 打包解出加载 + ABI 导出到 SDK 目录。
- [ ] **A 批 4（可选）**：`stage_types` 拆分 + route 契约归位。

**S2 Steps/Terminal 重设计**
- [x] T0：设计 + ADR-0004 R18–R21。
- [x] T1：`StepSetKind` + 三维 selection + 稀疏 catalog + 测试（真机 `T1` PASS）。
- [x] T2：`TerminalExecution<T::Input>` + `ActivationContext`（二进制与 T1 逐字节相同，T1 真机覆盖）。
- [x] T3a：native wire `steps`（backend 私有 section）+ `main` 使用 + 真机 `T3` PASS；Kotlin `steps` 字段 + `StepSetKind` 枚举。
- [x] **T3b**：Kotlin `text`/`bool` 访问器 + `backend.steps` 字符串解析；`value` 放参数末位保住尾随 lambda 调用点；
  `:profile-core:test` 13/0 通过、`:app:compileDebugKotlin`/`compileDebugUnitTestKotlin` exit 0（子智能体 A 执行）。
  注：T3b 代码随文档提交 `2fdf961`（当时误用 `git add -A` 收进），提交信息不含 T3b，内容无误。
- [x] **T3c**：文档/模板（子智能体 D，`d3b41ce`）；协议代码+资产+extractor（子智能体 C）。
  门禁（委派子智能体）：host/NDK/lint/`profile-core:test`/`app:testDebugUnitTest`(83)/`cargo test`(40) 全 PASS；
  `cmp_disasm --reviewed`：`do_one_write` OPERAND-SHIFT；**`consumer_thread` 单条 `cbz w8`→`tbz w8,#0`**
  系 `optional<uint8_t>::value_or(0)!=0`→`optional<bool>::value_or(false)` 的必然结果（寄存器/指令数/分支目标不变），
  已作为**显式带理由的 `REVIEWED_SHAPE`** 写入 `tools/cmp_disasm.py` 并复核，`RESULT: PASS`。真机 3 次冷机 2 PASS/1 FAIL(W1 flake) → PASS，记录 `T3c-…-direct-pass.md`。
- [x] **T3d**：「一般 / Shizuku / UMH」三选 UI（提交 `a427471`；`ExecutionModeMapping` 单一权威；UMH 置灰；未按 mode 覆写 profile 的 `backend.steps`）。
- [x] T4：`Cve2026_43499Backend<StepSet>`（`W1W2`/`W1W3`）+ `Cve43499Primitives` 非模板基类（提交 `91723e7`；host/NDK/lint/cmp PASS；`attack_write` 符号改 `Cve43499Primitives::*`，cmp 加拼写）。真机 `T4` PASS（一次冷机，multicast ×4→root→KernelSU）。
- [ ] T5：`umh_forward` 执行 + `RootProgram` 启动 + 真机。

**S3 43284 backend**
- [x] 评估 + L 级计划；B0 登记。
- [x] B1a：`contract/capabilities.hpp`（`KernelMemoryOps`/`FileCacheWriteOps`）。
- [x] B1b：`RootProgram`/`TerminalInput`（现由 S2 统一）。
- [x] B2：per-backend 状态构造（提交 `ec58b1a`；`BackendState` 概念 + `Pipeline` RAII；host/NDK/lint/cmp PASS；真机 `B2` PASS（一次冷机））。
- [x] B3：43284 known-but-unavailable identity/catalog/contract/wire 解码（提交 `59985fc`）。
- [x] B4：`backend.cve_2026_43284` 私有 section（7 字段，数值 policy token；strict 仅 backend==6 接受；manifest 94→101；提交 `663bc10`）。
- [~] B5：43284 原语/LKM 链。**设计已完成**：`cve-2026-43284-b5-design.md`（519 行，参考 DirtyFrag-Android-Root-Jailbreak/LSPromise/DFReroot/DirtyInit；选定 IpSecManager+CBC-16B+crash_dump+libc++ sentry+DFRoot LKM/UMH 链；B5-0..B5-7 分批）。
  **硬前置**：① 运行时参数通道（GLK1 v2 仅 u64，载不下 IpSec SPI/ports/32B+32B 密钥）需决策；② 需可审计且与 KMI 匹配的 `dirtyfrag.ko` + 可加载设备（公开发布 `.ko` 与源码不一致）；③ `BackendExecution` 需泛化到 `TerminalInput`（R10 延续），`platform/abi|vivo` 不存在。**未获资产前不实现、不标 supported**。
  追加（B5-0）：改造计划 `cve-2026-43284-refactor-plan.md`（428 行，28 个 vendored 文件逐文件映射）+ 目标模块骨架 `src/core/backend/cve_2026_43284/{ipsec,pagecache,lkm,steps}/`（仅头，不进构建）。**通道 A/B 待拍板（计划推荐 B：`--ghostlock-app-call` stdin 第二段长度前缀会话秘密帧，84B，密钥不落盘）**。
- [ ] B6：`umh_forward` 执行 + `RootProgram` 启动 + 真机（依赖 B5）。
- [x] B7：Kotlin backend 选择（`BackendKind` 含 43284 wire 6/available=false；HOCON `backend.kind`；UI 置灰；偏好迁移）——Gradle exit 0、golden 未动。
- 教训：委派重试前先确认原 subagent 确实结束（`send_message` 返回 not found 不可靠）；本轮出现两个 B7 并发写同一 Kotlin 文件，已中断重试并未造成损坏。

### T3c 精确范围（只读调研结论，2026-10-03）

**移除 `recommend_shizuku`**：
- native：`model.h:83`、`binary.cpp:63`（`kMeta` 表）、`profile_binary_test.cpp:96,117`；native 无其它消费者。
- Kotlin：`NativeProfile.kt`（:23 属性、:90 序列化、:393 `from`、:487/:512 Builder、:659 build）；
  `BuiltinProfileCatalog`（:15/:31-33/:65）、`UserProfileStore`（:102-112）、`AndroidProfileConfigController`（:778/:791）、
  `ProfileResolver.KnownTopLevel`（:14）、`AndroidGhostlockRepository`（:159-175/:927/:954）、`GhostlockModels.KernelSnapshot`（:19）、
  `GhostlockViewModel`（:87/:92/:98-103/:830）、`GhostlockUserService`（:47-52）、相关测试 5 个。
- HOCON：62 个 assets（56×`=0`、2×`=1`：`…ab14110541.conf:6`、`…ab14546557.conf:5`；4 模板 `=null`；`6.1.145-maybe-dirty.conf:29`）。
- Extractor/doc：`tools/extract_rs/src/report.rs:319`（`=0`）与 :449；README/PROFILE_SCHEMA/SUPPORTED_DEVICES/templates。

**HOCON 布尔项实际只有**：`recommend_shizuku`（移除）、`compact_waiter`（`route.*` 段，25 文件）、`recommend_vr_guard`（1 文件）。
`safe_mode` **不在 HOCON**（仅 wire `meta.safe_mode`，`patchSafeMode()` 运行时改写）——branch-plan 之前把它列为 HOCON 项有误，已修正。

**native 标志改 `bool`**（候选，须保 `sizeof`/尾部 padding 与攻击代码 byte-identical）：
- `ProfileMeta::safe_mode`（model.h:85）、`KernelMisc::vr_guard`（:123）、`KernelMisc::compact_waiter`（`optional<bool>`，:118）；
- 连带：`binary.cpp:12-28/61-68/116-122/192-197`、`model.h:308-314` 访问器。

**bool 解析缺口（必须一并修）**：`getLongAt` 只认 `Number`；当前 `from()` 用 `vu("recommend_vr_guard")`（NativeProfile.kt:394）与
`vbOrNull("compact_waiter")`（:443，经 `nativeValue` 分支映射），改 T/F 后会**静默丢值** → 换 `bool` 访问器，并让 `compact_waiter` 分支查找支持布尔。
## 3.5 wire 重设计（GLKv3，跨流前置）

- **GLKv3 = 纯 MessagePack 文档**（根 map + 必填 `schema`），解析用成熟单文件库 **MPack**（MIT）；**无 magic/独立头**，
  I/O 分帧（stdin 4B 长度前缀）保留；**静态策略进文档，运行时密钥走会话帧、永不进文档**。
- 设计与决策：`wire-transport-model.md`（**定稿：MessagePack**）。
- [x] **GLKv3-1**（native，提交 `ea4870d`）：MPack 接入（C 编译、告警隔离）+ `profile/glkv3.{hpp,cpp}`（expect API 零拷贝 decode、canonical encode、schema 校验、fail-closed、界）+ `glkv3_codec_test`（往返/确定性/拒绝向量/fuzz）。host/NDK/lint/cmp 全绿；生产未切换。
- [x] GLKv3-2 Kotlin canonical 编码（`org.msgpack:msgpack-core:0.9.12`，提交 `e20a547`）：UTF-8 键序/最短整数，4 组 native 逐字节 golden 对拍；`:profile-core:test` 36 / `:app` 96 全绿。
- [x] GLKv3-3 path→type schema（43499 94 项 / 43284 7 项）+ Kotlin 适配器 + manifest v3（提交 `f4ea1c6`）。
- [x] GLKv3-4 生产/导出切 v3（写 v3、读 v3+v2，提交 `af0c9c0`）+ v3 golden + **真机 PASS**（记录 `GLKv3-20261003-multicast-direct-pass.md`）。
- [x] **GLKv3-5 移除 v2 写路径**（提交 `1860bda`）：native v2 `serialize` 收进 `GHOSTLOCK_ENABLE_V2_WRITER`（仅 host 测试编译），设备 binary 无 v2 写符号；Kotlin v2 写函数 `@VisibleForTesting`；**v2 只读保留**。GLKv3 迁移收尾。
- [x] B5-1 运行时会话秘密帧（通道 B，提交 `0de4ed8`）：84B（BE）+ 4B 长度前缀；native `session_frame`/`ipsec::zeroize`、仅 43284+frame 路径读、fail-closed；Kotlin `SessionSecretFrame`；golden 逐字节一致；门禁全绿。生产 wiring 待 IpSec 产密钥后接入（B5-7）。
- [x] B5-2 `ipsec/` 原语（提交 `c7ded3c`）：AES-256 ECB/CBC、HMAC-SHA256、ESP 布局/ICV/verify、`compute_cbc_iv`；FIPS-197 / SP800-38A / RFC 4231 KAT + IV 恒等式；Odzhan BSD-3 署名。门禁全绿。
- [x] B5-3 `pagecache/`（提交 `86b3303`）：`write16`/块序列 + 可注入 `SpliceIoOps` + `FileCacheWriteOps`；fake splice/pipe host 测试（并抓出 seq 偏移 bug）。
- [x] B5-4 `lkm/`（提交 `2052b97`）：8 KMI 表 fail-closed、`.ko` ELF/`.modinfo` 预检、UMH `late-load` argv（无 shell 拼接）；host 测试；门禁全绿。
- [x] B5-5 `steps/` ELF+Hook（提交 `9e463b2`）：ELF64/AArch64 解析、符号定位、hook 方案（BTI/PAC 拒绝、relocation 冲突）、shellcode 模板；合成 fixture host 测试；门禁全绿。
- [x] B5-6 `steps/` chain（提交 `9999aed`）：编排（写→校验→触发→等待→清理）、载体回退（写前才回退）、写失败/校验失败回滚、终结点恰好一次；host 注入测试；门禁全绿。
- [x] B5-7 `backend_terminal` + `platform`（提交 `27792ee`）：`DeviceProbeOps`/`DeviceFacts` fail-closed、`UmhForwardInput`、`run_backend_terminal`（steps→facts→LKM→UMH→carrier→chain）；fake 覆盖；门禁全绿。
- [x] B5-8 组合（提交 `7aabcb2`）：43284 三元组接入 dispatch（`combination_supported`=已接线）、`selection_supported` 为运行时 fail-closed 闸（43284 仍 false）、`run_orchestrated_pipeline` 顶部 guard、`UmhForwardPolicy::run` + `UmhForwardChannel`；编译期契约 + fake 覆盖；43499 未动。
- [x] B5-9a 真机**只读**诊断（提交 `950cbe4`/`818b99d`）：设备事实齐备、KMI `android13-5.15` 匹配、内置 `.ko` 预检 pass → `ready`（PASS）。
- [x] B5-9b 修复（提交 `950cbe4`）：kallsyms 受限/`selinux_state` 符号缺失降级为记录项（不再 device_blocked）。
- [x] B5-9c 真实 ChainOps + 分阶段入口 `--run-cve-2026-43284 <ko> <target> --stage=plan|write|trigger|full`（提交 `352cab5`）；生产仍 fail-closed。
- [x] B5-9d 写原语真机 PASS（`f0f09a6`）：`--stage=write` `written=698 verified=698`，目标内容变为 ELF 头（`.ko` 写完）。
- [x] 安全网：分区备份 + 冷机复核（`6d7f201`）+ AVB guard 基线/检查/告警/修复（`895c467`/`a37cb31`），26/26 一致（`metadata` 预期可变）。备份在**仓库外** `../ghostlock-device-backup`。
- [x] B5-9e（提交 `68abcfb`）：patch #1 内嵌自建 splicehelper、crash_dump 读桥、libc++ hook 应用/恢复、LKM 预检接线、终态判据（`/dev/df`、`/sys/module/dirtyfrag`）、退出码对齐上游；host 门禁全绿。
  ~~已知缺口~~ 已在 B5-9f（提交 `536dc5f`）闭环：写侧 `OldPageSource`（pread 被拒时经 crash_dump 桥取旧页）+ 内嵌 `libcxx_blob`（472B，源 `libcxx.S@de2ab7b`，sha256 `d226d2e7…`）与参数化绑定（carrier/selinux ctx/insmod/mutex/attr_exec）。
  **① 已闭环**：B5-9g（提交 `a329104`）实现 `file_fd<0` 的 `HelperWriteSource`（helper 把 vendor 页 splice 进写管道）。
- [x] **写前断言**（提交 `bc86eeb`）：目标边界/区域闭合/pre-image 断言 + `run.target` 诊断（offset/len/size/preimage）。
- [x] **`cmp_disasm` 重定位归一化**（提交 `bc86eeb`）：`adrp`+内存操作数按**有效地址**比较（非白名单；只读数据不同字节仍 `RELOC-DIFF` FAIL），严格模式行为不变。
- [x] **安全网扩展**（提交 `f528c13`）：文件级页缓存守卫（`files-baseline`/`files-check`）、`verity-status`（dm-verity `V/E`）、vendored AOSP `avbtool` + `avb-verify`（hash/hashtree 描述符）、`check-all`。
- [x] **设备端 `avbcheck`**（提交 `c39a117`）：NDK 静态 aarch64 原生工具（`hash`/`baseline`/`check`/`avb-verify`/`verity-status`），**本机**读 `/dev/block/*`，含 **raw RSA 签名校验**（`sig_ok=5`，含 chain）；真机 `avb-verify --slot _a` 12 描述符全 `[ok]`、`verity 34×V / E=0`；与宿主 Python 摘要逐字节一致。
  **剩余（B5-9h）**：① 设备侧资产：`/system/lib64/libc++.so` 镜像、按真机 sentry 前导指令选 `hook_guard`、vendor 载体路径；② 冷机 `plan→write→trigger→full` 门禁（每步前后 `avbcheck` + `files-check` + `verity-status`，异常 `alert`+`repair`）；③ 通过后翻转 43284 可用性并归档。
- 它与 S1（A2-3c）与 S3（B5 通道）都有交叠，故列为本分支的**跨流前置**：先定 GLKv3，再继续 A2-4/5、B5。

## 4. 唯一执行顺序（下一步）

```
已完成：T0 → T1 → T2 → T3a → T3b → T3c → T3d → T4
        GLKv3-1..5（wire 迁移，真机 PASS）
        B0 → B1a → B1b → B2 → B3 → B4 → B5-1..B5-9g
        安全网：分区备份/冷机复核 + AVB guard + 设备端 avbcheck
待办主线：B5-9h（43284 真机 plan→write→trigger→full，含资产与可用性翻转）
随后：    A3-2 -> C（S1 剩余；A2-4/5 已完成，B 文档/对拍同步已完成）；T5 由 B5-8/B5-9e 的 UmhForwardPolicy 覆盖，待真机
可选：    DirtyInit 式（init 中继）/ DFReroot 式（system-UID 持久化）作为 terminal #3/#4
```

- **默认下一步 = B5-9h**：先在设备端跑 `avbcheck avb-verify --slot _a` + `check-all` 建立当前态；再冷机（KernelSU 未加载）→ 43499 取 root → `plan`（只读）→ `write` → `trigger` → `full`，每步前后跑 AVB/文件/verity 检查；异常 `alert`+`repair`。
- 需要设备侧资产：`/system/lib64/libc++.so` 镜像、按真机 sentry 前导指令选 `hook_guard`、vendor 载体路径；`.ko` 预检已 PASS。
- 真机通过后才翻转 `backend_available(43284)`/`terminal_available(UmhForward)` 并归档 `device-gates/`。
- 每批门禁：host + NDK + lint；触攻击路径/公共契约加 `cmp_disasm` + 真机并归档；
  `cmp_disasm` 按 ADR-0004 第九轮定位（必跑、记录差异理由；`--reviewed` 现含 adrp 有效地址归一化）。

## 5. 当前基线

- HEAD 以 `git log -1` 为准；工作树按批次提交；`build/native/ghostlock-B0` 为攻击函数基线。
- 备份/安全网：仓库外 `../ghostlock-device-backup`（22GB，26 分区 + 文件基线）；`tools/device-guard/`（guard）与 `tools/avbcheck/`（设备端）。
- **攻击函数基线 `build/native/ghostlock-B0` 已刷新**为当前 HEAD 构建（sha256 `fc44c344…`，自比 6/6 IDENTICAL）。之前 B0 已过期，`--reviewed` 里的位移多为 B0↔HEAD 既有差异；后续批次应期望「无新增差异」。
- 已通过的整体真机证据：`P0-01`、`A3-01`、`A2-1`、`A2-2a`、`A2-2b`、`A2-2c`、`A2-2d`、`A2-2e`、`A2-3a`（第 3 次）、`A2-3b`、`B1b`、`T1`、`T3`（native）。
- 设备：A301SO / `5.15.189-android13-8-00016-…-ab14546557`；冷机、固定 CPU 对 0/1。
- adb：USB 拔线后现走**无线调试**（`adb-QV770MFGJ1-XxNlpb…`）；USB serial 仍为 `QV770MFGJ1`。

## 6. 约束与保留

- ADR-0004 R1–R21（含 R18–R21）；错误分层 E1–E3；所有权 O1–O5；控制流 CF1–CF5。
- GLKv3（MessagePack，无独立头）文档中性，v2 只读兼容、不改版本号；profile 唯一配置权威；`kernelsnitch/` 上游冻结边界。
- 新计划/分册出现时，**只在本文件登记一行并给出权威指向**，不再各自维护全局顺序。

## 7. 变更记录

- 2026-10-03：建立分支总 plan；合并 S1/S2/S3 顺序。
- 2026-10-03：T1/T2/T3a 完成；T3 拆为 T3a–T3d，明确 **T3b→T3c→T3d→T4→T5** 的顺序；HOCON T/F + C++ bool 并入 T3c。
- 2026-10-04：T3b/T3c/T3d/T4、GLKv3-1..5、B0–B8、B5-1..B5-9g 完成；安全网（分区备份 + AVB guard + 设备端 avbcheck）就绪；下一步收敛为 **B5-9h**（43284 真机收尾）。
- 2026-10-04：**B 契约/自动化同步完成**：文档同步到现状（AGENTS/README 双语文/core README/adding-a-component），host 对拍与 Kotlin 对拍实跑通过；S1 剩余收敛为 **A3-2**、**C**（A 批 4 可选）。
