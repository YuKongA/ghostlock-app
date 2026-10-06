# 分支总 plan（vr-ko-bypass-dev）

> **唯一入口**：本文件回答「这个分支在做什么、现在到哪、下一步做什么、哪个文档算数」。
> 状态变化只改本文件的进度表；分册负责细节，不再各自维护全局顺序。
> 基线：93afccc。分支：vr-ko-bypass-dev。
> 最近更新：**2026-10-05**（S4：R0–R4、R6a、**R6b/T5**、**F1/F3/F4/F5**、**R2b**、**P1 插件接口**、**R8** 均已落地且各有真机门禁（`s4-f4-f3-f5-20261005-pass.md` / `s4-r2b-20261005-pass.md` / `s4-p1-probe-20261005-pass.md` / `s4-p1-plugin-wire-20261005-pass.md`）；43284 app 域直连打通；契约层 α–δ 完成；LKM 通道端到端 PASS；构建环境移出 iCloud）。

---

## 0. 状态总览（做了 / 没做）—— 只改这一节即可反映全局

### 0.1 ✅ 已完成 + **有真机门禁证据**

| # | 项 | 证据 |
|---|---|---|
| 1 | S1 框架收敛（A2-4 全部、身份词汇→contract、A2-5、A 批 4、A3-1、A3-2①②③） | device-gates/（P0-01/A3-01/A3-2…）；防火墙 159 files / 4 edges / 0 unexpected / 0 stale |
| 2 | S2 Steps/Terminal（T0–T4；T5 由 S3 的 B6 覆盖） | 真机 T1 PASS；T4 真机 PASS（multicast ×4 → root → KernelSU） |
| 3 | S3 B0–B6 生产接线 + 可用性翻转（869a43c）+ 生产缺陷修复（76b18c1/3be4622） | — |
| 4 | S3/B5-9h 全链（自建 LKM；AVB ok=12 fail=0） | device-gates/B5-9h-20261004-full-chain-lkm-pass.md |
| 5 | S3 **43284 生产 app-call 门禁**（adb 免 App 试验台，EXIT=0） | device-gates/43284-production-20261005-pass.md（8b4e037） |
| 6 | 契约层 **α/β/γ**（能力词汇/接口、Capabilities 进会话、ABI 归一 + plugin/ 收口） | 1e1dac8 / 83d2df8 / 662a5f2；β、γ 各跑过 43499 真机门禁 |
| 7 | 契约层 **δ-1/2/3**（LKM 版本化通道 + 窗口接线 + 会话绑定驻留） | delta-lkm-channel-20261005-pass.md、delta3-lkm-session-20261005-pass.md |
| 8 | **δ-4 插件活化**（Capabilities → glk_contract_ops、窗口内 POST_TERMINAL 同步派发、fail-soft） | delta4-plugin-lkm-e2e-20261005-pass.md（真 .so 插件 calls=4） |
| 9 | **43284 app 域直连**（设备事实降级 + umh 探针域感知 + Kotlin 撤销强制 Shizuku） | 真机日志：device_facts degraded=proc_version,selinux；starting native:（非 UserService） |
| 10 | 安全网：分区备份（仓库外）+ 设备端 avbcheck + 文件级页缓存守卫 | tools/device-guard/；历次门禁附带的 AVB 12/0 |

### 0.2 ✅ 已完成（仅 host / NDK / lint / Kotlin 测试）

| # | 项 | 证据 |
|---|---|---|
| 11 | Kotlin/profile-core 门禁 + 会话帧逐字节对拍 | :app:testDebugUnitTest、:profile-core:test 绿；native session_frame.cpp ↔ Kotlin SessionSecretFrame 一致 |
| 12 | **构建环境修复**：Gradle 输出移出 iCloud（build 符号链接；输出落 ~/.ghostlock/build） | 重复副本 0；javac/dex 不再 defined multiple times；.gitignore 已含 build.nosync/、/build |
| 13 | γ 批目录合并（plugin/）与类型名收口（Ancillary* → Plugin*） | d2a526d；真机 43499 PASS |

### 0.3 ❌ 未做（按优先级）

#### S4 配置/wire 重设计 —— R0–R4 / R6a / R6b / F1–F5 / R2b / **P1** / **P2** / **R8** 已落地；剩 R5（+ P3/接线待办）

- [x] **R0** 计划 + ADR：config-wire-redesign-plan.md（384 行，R1–R8、批次 R0–R5、证据附录）
- [x] **R1**（`38f1343`）：`FieldSpec` 增 `default/source/wire/doc` + `DefaultValue{Literal|Derived|Convention}` + `SchemaRegistry` + `bind_all(registry,mode)` + `default_used=` 诊断；
      默认/推导逻辑从 backend 上收（`selinux_ctx`/`kmi`/`lkm_path`/`late_load_args`/`carrier_path`/root package）。**wire 零改动**（manifest 101 字段不变）。
      **真机门禁 PASS**（`device-gates/s4-r1-20261005-pass.md`）：43499 冷启 + 43284 app-call；解析值 `kmi=5015 / lkm_path=<HOME>/helper.ko / selinux_ctx=0 / late_load_args=0` 与 pre-R1 **逐项一致**；AVB 12/0。
- [x] **R2**（`7bc6eb2`）：wire 原地增量——**owner-qualified 段名**（`backend.cve_2026_43499.*` / `platform.abi.*` / `common.*` / `countermeasure.vivo_vr_guard.*`；`backend.cve_2026_43284.*` 不变）+ `string` 类型（上限 256）+ **选区感知文档** + **manifest v4**（7 列，101 字段）+ Kotlin **生成式**映射。
      **核心目标达成**：**43284 文档 13 段 → 2 段**（仅 `backend.cve_2026_43284` + `common`）；43499 = 17 段。
      **真机门禁 PASS**（`device-gates/s4-r2-20261005-pass.md`）：43284 app-call `EXIT=0` + `profile_resolved` 值不变 + `lkm_window opened=1`；43499 冷启 PASS；**负向：1.2 时期旧段名文档被拒**；AVB 12/0。
- [x] **批 A · 版本统一为 3**（`43713de`）：63 个资产 `schema_version = 3`；Kotlin **写出恒 3**、**唯一迁移点 `LegacyProfileConverter.normalizeSchemaVersion`**（接受 3；legacy `1` 与**缺键**归一为 3 并记 stderr 诊断；其它值拒绝）；extractor 产出 3；App `1.3`。
      门禁：host 44 ok / 防火墙 172-0-0 / NDK 0 告警 / lint 0 / Kotlin 测试 EXIT=0 / `cargo test` 41 passed。
- [x] **批 B · R2c v3-only 割接**（`acaa0ac`）：删除 `profile/binary.cpp`（v2 framer+writer）、`-DGHOSTLOCK_ENABLE_V2_WRITER`、v2 manifest（`profile-manifest.tsv`）与 4 个 v2 测试/测试块；`entry.cpp` 非 map 根直接拒；新增「v2 被拒」host 用例。
      **真机门禁 PASS**（`device-gates/s4-r2c-20261005-pass.md`）：43284 app-call `EXIT=0`；43499 冷启 PASS；**真机喂 v2 旧 bin → `EXIT=255` 被拒且不 panic**；AVB 12/0。host 59 ok / 防火墙 171-4-4-0-0 / NDK 0 / lint 0 / Kotlin EXIT=0 / cargo 41 passed。
- [x] **批 B2 · R2c-2**（`84d0564`）：`orchestrator.hpp` 判据改 `contract::BackendKind::Cve2026_43284` + **删除 `profile/binary.h`**（framing 迁 `ghostlock::profile`）+ 删 Kotlin v2 codec（`toBinary/fromBinary`/`nativeDocumentV2`；`patchSafeMode` 保留为 GLKv3 委托）+ 迁移 v2 白盒测试。
      **真机门禁 PASS**（`device-gates/s4-r2c2-20261005-pass.md`）：43284 app-call `EXIT=0`（值不变）、43499 冷启 `EXIT=0`、AVB 12/0；防火墙 **170**-4-4-0-0；判据 `grep toBinary|fromBinary|binary_profile::`（非 tests）为空。
- [x] **R4 · 43284 policy 完整表达**（`66ab498`）：`WireKind::String` 端到端（`ReadResult` 持有解码缓冲、≤256 B fail-closed）+ `carrier_path`/`lkm_path`/`defex_symbol` 改 string 路径（删 token 表）+ 握手参数入文档（`wait_timeout_ms=15000`/`module_poll_attempts=40`/`module_poll_interval_ms=5`）；manifest **104 字段**。
      **真机门禁 PASS**（`device-gates/s4-r4-20261005-pass.md`）：默认值不变；**移走默认 ko 后自定义 `lkm_path` 仍跑通**；不存在路径 binding 失败 `EXIT=255`；43499 冷启 PASS；AVB 12/0。
- [x] **R3 · HOCON 布局重排**（`d6254c4`）：canonical `ghostlock{ selection{} common{} platform{abi{}} backend{ cve_2026_43499{…} cve_2026_43284{…} } countermeasure{} }`；67 资产迁移 + `index.conf` `backends` 矩阵；`ProfileLayout` 双布局归一（未识别键 fail-closed）；**扁平化等价 67/67**；extractor 输出新布局；wire/native 零改动。
      **真机门禁 PASS**（`device-gates/s4-r3-20261005-pass.md`）：43284 app-call 值不变、43499 冷启 PASS、AVB 12/0。
- [x] **R6a · fallback 出栈**（`be7b58e`）：删 `common.fallback_route`（wire+schema）+ native `meta.fallback_route`/`fallback_route()`/`route_policy` fallback 分支/`fallback_used`/`can_fallback()`；58 资产删键（17 个再删 fallback-only 分支）；Kotlin/UI 全清（旧 `fallback.*` 仍识别并忽略）；manifest **103**、金标 58/58 重算。
      **真机门禁 PASS**（`device-gates/s4-r6a-20261005-pass.md`）：43284 app-call 值不变 + `lkm_window opened=1`；43499 冷启两次 PASS；AVB 12/0。**教训：跨批次的旧导出 bin 会被 fail-closed 拒绝，门禁必须用当批导出的文档。**
- [x] **ADR-0006 · terminal 归属**（`4fc922b`）：取证「名义两轴、实际已泄漏」（`terminal/root_child.cpp` 直连 43499 state 7 处；`main.cpp` 直连 43284 `session_frame.hpp`），而通用件很小（root_program 17 行 / umh_command 35 / terminal_input 110 / handoff_probe 无依赖 / root_script 11 KB 仅 1 处耦合）。
      **决策：词汇保留、实现下放 backend、共享件抽出、入口去 backend 化**；重估判据：某 terminal 被 ≥2 backend 原样复用则上提。
- [x] **R6b + T5 · 组合 token 白名单**（设计 v2 `3249d7d` + v3 补丁 `114cfb7`；`7169801`，门禁 `device-gates/s4-r6b-20261005-pass.md`）：token 归 **`backend.<id>.steps`**（非顶层），`contract::kCombinationCatalog` 为唯一权威——**12 token = 7 可用 + 5 计划**（可用：6×43499 的 `{mcast,pselect,tcp}_{rootchild,shizuku}` + 43284 `umh`；计划：`{mcast,pselect,tcp}_umh` + 43284 `rootchild`/`shizuku`，解析接受、门禁拒绝、UI 置灰）；43284 无 route 轴 → 裸 path 名；catalog 收敛为 `(backend, token)`；
      `backend.<id>.steps` **uint → string token**（旧 uint 兼容映射 + `legacy_steps_id` 诊断）；根 `route`/`terminal` 降为**一致性校验**；计划项解析接受、门禁拒绝；
      Kotlin 单一下拉（计划项置灰）+ 62 资产迁移 + `ExecutionModeMapping` 降为派生；extractor 产出 token（`--steps-path`）；manifest 两行 `str` + 金标 58 条重算。
      **真机**：`mcast_rootchild` 冷启 PASS（20s 内 KernelSU ready）；`pselect_rootchild`/`tcp_rootchild` 因本机无该几何**干净拒绝**（EXIT=255，非失败）；43284 `umh` app-call EXIT=0 值不变；AVB 12/0。lint **0** → 审查 F2（padding）已解决。
- [x] **TERM T1–T4 · terminal 归属（ADR-0006）**：`root_script` 参数化（**脚本文本 sha256 逐字节一致**）、43499 terminal 实现下沉 `backend/cve_2026_43499/terminal/`、43284 新增 `entry.{hpp,cpp}` 缝合（`main.cpp` 直连 backend include **4 → 1**，无新增堆分配）、`terminal/` 零 backend include。
      ⚠ **过程偏差（F7，如实记录）**：T1–T4 的实现与 R6b **混在同一个提交 `7169801`**（我用 `git add -A` 扫了共享树）；两者因此共享同一份真机门禁（门禁跑的就是这份合并树，结论仍有效），但**历史无法按批归属**。今后严格执行「同一时刻一条写入流 + 批次原子提交」。
- ⚠ **过程偏差（F7 第二次，2026-10-05；如实记录，责任在 Lead）**：`b3258bbc`（P1 Kotlin half-A，kotlin-app 16 文件）**混入 native-core 在途的 `profile-manifest-v3.tsv` 两份各 +4 行**（`plugin.*` 静态行），提交信息未提及——由 kotlin-app 只读发现；根因是暂存用了宽泛路径（`git add profile-core app`）。
      **影响**：无功能影响（静态行本身正确且随 HEAD 生效），但该提交不再按 scope 自洽、`git log` 无法按模块归属；**纠正措施**：暂存一律**显式路径**（`git add <file>…`），禁止目录级暂存；与 F7 首例同规（同一时刻一条写入流 + 批次原子提交）。
- [x] **P1 · 插件接口（探针 + ABI + wire + Kotlin 导入）**（native `d951dd38` 第一步 / `4d25d6e7` 第二步；Kotlin `b3258bbc`/`0ff79abd`/`4ecc87a6`/`cd2c6cd2`/`0b1c0fa0`；契约 `contract-design.md` §3.14.7，门禁 `device-gates/s4-p1-probe-20261005-pass.md`）：
      **native**：`--plugin-probe <path.so> [--expect-sha256 <hex>]` 只读探针（`plugin/probe.{hpp,cpp}`：不注册/不运行 hook、哈希先于 dlopen、独立进程 + `PR_SET_NO_NEW_PRIVS`）+ C ABI **尾部追加**（`glk_param*`、`glk_module` 尾部字段；**不 bump** `GLK_ABI_VERSION`）；第二步 `WireType::Union`（`kUnionScalarTypes` 恰好 4 成员）+ 两条动态行 `plugin.<id>.params.*`/`.extract.*` + `plugin/wire.{hpp,cpp}` fail-closed 绑定（`enabled` 必须 true、`stage` ∈ 4 host token、`module_path` 相对无 `..`/反斜杠、`module_hash` 64 位小写 hex、未知字段拒绝、≤16 插件不静默丢弃）+ `plugin_descriptor_declares` **size 门控**（v1 模块无尾部声明 → 拒绝）；manifest **103 → 109**；防火墙 172 → 174 → **177**（`3a952eb7`）；
      **Kotlin**：导入链（有界拷贝 + 本地 SHA-256 + 探针 + 原子安装 + no-backup 注册表）、描述符驱动参数行（只读）、`PluginProbeGoldenTest`（设备 golden 硬断言）、adapter 的 `|` 联合解析（未知成员 fail-closed、`<id>` 占位行无隐式前缀）；Gradle 绿（批内报告）；
      **门禁**：① 探针真机 PASS（`s4-p1-probe-20261005-pass.md`：哈希不匹配/路径不存在/非库文件/入口互斥四类负例 + 11 行 golden）；② **wire 层真机 PASS**（`s4-p1-plugin-wire-20261005-pass.md`：正确插件段被接受且 43284 全链 `EXIT=0`、`lkm_window opened=1`；`enabled=false` 与坏 `module_hash` 均 fail-closed）——**边界（必须与「通过」同读）**：本次未观察到插件的**运行时调用**，`src/core/pipeline/**` 对 `plugin::{controller,host_ops,registry}` **零引用**；P1 交付的是「声明 → 校验 → 绑定」wire 层；「加载 → 按 stage 调用 → 卸载」的运行时接线随后由 **`task-9` step 3a** 落地（**43284 已接线**：组合根 host + LKM 驻留窗口内 `POST_TERMINAL`，host/lint/NDK **0/0/0**，**真机门禁 PASS** `device-gates/plugin-runtime-3a-20261005-pass.md`；**43499 的 `pre_terminal` = step 3b，未开始**）——**注**：本条门禁的「未观察到运行时调用」是 2026-10-05 P1 当时的边界，已被 step 3a 取代，两条必须一起读；
      **设计修正（2026-10-05 裁决）**：**`plugin.conf` 资产取消**——插件配置走既有覆盖存储，不加资产、不改 67 个资产 include；`ProfileLayout` 白名单接受 `plugin.<id>.*` 并 fail-closed（`contract-design.md` §3.14.7.5）；
      **过程偏差**：两处 F7 见上方条目（`7169801`、`b3258bbc`）；本批未改 wire 版本（仍 `schema == 3`），探针/绑定不在攻击关键路径。
- [x] **P2 · extractor 投影（`--plugin-descriptor`）**（`593e51de`；文档 `1143f401`/`18ac3cb8`/`79b428c6`）：extractor 新增 `--plugin-descriptor <probe-stdout.tsv>`（**可重复**、**仅 `--format conf`**）——严格解析探针 7 列 TSV → `PluginDescriptor`；取值按 **R1**（读回正在产出的 profile 字面量）/ **R3**（BTF `struct.<s>.<f>` 偏移或 `sizeof.<s>`）/ **R2**（kallsyms 符号，基址相对）；写入 `plugin { <id> { extract { … } } }`（**仅 `extract`**、可导入**片段**、追加在 `countermeasure` 之后、无条目时输出**逐字节不变**）；`required` 缺失 = 硬错误、`optional` 缺失 = 省略（**default 不顶替**）、声明类型与解析值矛盾 = 硬错误；HOCON 键简单 token 裸写、含点/特殊字符加引号（R1 的 owner-qualified 键始终加引号，与 App `HoconSupport.keyName` 一致）。
      **门禁**：`(cd tools/extract_rs && cargo test --release)` **57 + 4 全绿 / EXIT=0**；强制重建 **0 warning**。
      **待办**：① Kotlin 侧 `extract.*` 发射（闭合 P2 环，kotlin-app 在途）；② **P3 参考插件**（**已落地并移出为独立项目 `ghostlock-plugin-example`**，`940c404f`/`1c70b3e4`；本仓库只留 `tools/plugins/README.md` 指引）；③ **插件运行时接线 = step 3**（native-core step 2 的 host 之后；当前 pipeline 对插件宿主零引用）。
- [ ] **PAYLOAD · 接管后三档 payload（设计已定稿 + 用户已确认；实现待 native 半场）**（设计 `docs/analysis/terminal-payload-tiers-design.md` r2 / `c335aabc`；契约 `contract-design.md` §3.15）：
      **段**：新顶层 owner `payload`——`payload.tier` ∈ {`exec`,`script`,`ko`} + **单档互斥**；`exec.command`(argv)/`.sha256`、`script.path`/`.sha256`、`ko.count` 1..8 与 `ko.<i>.{path,sha256}`；**无 `payload.*` ⇒ 文档逐字节不变**；
      **安全边界**：相对 `<GHOSTLOCK_HOME>`、禁绝对/`..`/反斜杠/NUL + realpath 二次校验、≤256 B、可选哈希钉（**比对前不执行**）、先 `stat` 限长再读、`ko` **必须过 `lkm::precheck_module_file`**、**argv 不做 shell 拼接**、App 分档授权 + 可撤销 + **执行前摘要**；
      **失败语义**：**绝不中断攻击链**；用户显式请求未完成 ⇒ 本次运行结论标「**未完成**」+ `payload_error` / `ko[i]=<reason>`；**提权记录照记**；`ko` 逐项继续、不回滚；
      **可用性矩阵**（terminal × tier）**native 导出**、Kotlin 不硬编码、绑定期 fail-closed（沿用 `stage_availability` 纪律）；
      **依赖**：native 半场（wire 校验 + 绑定 + 接管后执行器；**先随 `root_child`**，出现第二个复用点再上提）× Kotlin 半场（layout/校验/分档设置页）；
      **门禁**：host / NDK 零告警 / lint 0 / Kotlin 测试 + **真机三档正例与负例**（`exec` rc=0；`script` 标记文件；2 个 ko 一好一坏 → `ko[1]=<reason>`、攻击链 PASS 但本次标「未完成」；sha 不符 / 路径含 `..` / 超限 / 矩阵外 → 拒绝且**不执行**）+ **无 payload 回归**（逐字节）+ AVB 12/0；结果按 `device-gates/` 归档；
      **结构同步（已同批完成）**：UML §1（C4c/C6c）、§3.1（`ghostlock__payload`）、§3.2（`data.payload`/`PayloadSettingsUI`）+ AGENTS 的 owner 白名单。
- [ ] **ROOT-MANAGER · 默认档 root 管理器选择（设计稿已起草，待用户确认）**（设计 `docs/analysis/root-manager-selection-design.md`；契约占位 `contract-design.md` §3.15.8）：
      **定位**：归入 payload 轴——`payload.tier = "root"` + `payload.root.{kind, manager, argv}`（白名单、native 唯一权威、导出给 Kotlin）；**无 `payload` 段 = 现状 KernelSU/ksud 逐字节不变**，**显式 `kernelsu` ≡ 不写**；
      **现状事实**：`RootProgramKind`/`RootProgram` 已在 `contract/identity.hpp:366-380` 但 **wire 零键**；运行时硬编码 `$GHOSTLOCK_HOME/ksud`（`execution_binding.cpp:49-56`）；late-load 无 shell/无拼接（`lkm_image.hpp:85-92`）；包名权威 = KernelSU→`me.weishu.kernelsu`、其它→空（`schema.hpp:68-77`）；
      **外部事实（Lead 核实，引 URL）**：FolkPatch README 自述「non-parallel extended branch of **APatch**」（<https://github.com/LyraVoid/FolkPatch>），`app/build.gradle.kts` 实测 `applicationId=me.yuki.folk`/`namespace=me.bmax.apatch`；KernelSU-Next / SukiSU-Ultra / ReSukiSU 的 Gradle 路径核实尝试**均 404** ⇒ **分支包名不得猜，必须逐项核实**；
      **UI**：默认档展开为单选；**未实现者标「计划中」并置灰**（沿用既有模式，不假装可用）；矩阵由 native 导出、Kotlin 不硬编码；
      **一等检查（用户第三条，P1 必做）**：**管理器是否存在、是否可启动**——App 预检（包安装 / `ksud` 或指定路径可执行 / 哈希匹配）+ **native 绑定前复核**（权威在 native）；不存在/不可启动 ⇒ **不降级、不猜替代品**，结论标「未完成」+ 原因（不中断链路）；
      **P1（可落地）**：`kernelsu`（**KernelSU 及分支共享 `ksud`** ⇒ 默认按 ksud 走、行为与今天等价；`manager` **可选**包名，缺省用 `me.weishu.kernelsu`）+ `custom`（用户指定程序/argv，不猜包名）+ wire/白名单/UI 旋钮 + **存在性/可启动检查（App 预检 + native 复核）** + 穿到既有 late-load 路径；门禁 = host / NDK 零告警 / lint 0 / Kotlin 测试 + **真机 43284 无 payload 逐字节回归 + 显式 kernelsu 等价 + 43499 冷启回归** + AVB 12/0；
      **P2（计划中置灰）**：`folkpatch` = **加载 KernelPatch 模块**（官方文档 E4：`apd insmod` 手动重定位 + 绕过 modversions(CRC)/vermagic + `init_module` + **软重启生效**；前置条件 = SELinux Permissive + 已 Root，**正是 GhostLock 的 W1/W2 产物**）——**不复用** ksud late-load，需独立机制设计 + **独立真机门禁（含软重启后 Root 生效 / 重启后失效）**；**KernelSU 分支不再需要机制白名单**（共享 ksud，`manager` 可选手填）；
      **待用户确认**：① KernelSU 及分支清单；② FolkPatch 机制/包名/入口；③ 制品来源（倾向既有导入 + 哈希）；④ `manager` 最终形态；
      **本批已同步**：UML §1（C6d）+ §3.1（`RootProgramKind`/`RootProgram` 与 payload 关系）、`contract-design.md` §3.15.8 占位。
- [x] **规则溯源 · 三条真机缺陷 → 规范 §4.4（2026-10-05；提交号待 Lead 补）**：同一模式 24h 内连续三次真机缺陷，全部**已修**，规则已入 `docs/development/engineering-standards.md` **§4.4 状态/投影字段语义**（强制）：
      **① `recommend_shizuku` 升级即崩**（修复 **`9883a271`**「legacy validation drops unknown keys with a diagnostic instead of aborting」）：把「**旧版本遗留数据**的验证」写成 fail-closed 的 `require`（`ProfileLayout.validateLegacy`，`profile-core/.../ProfileLayout.kt:480`）⇒ 升级即崩；正确=**旧数据容忍 + 诊断**，只有本版本撰写的输入才 fail-closed（规则 4）；
      **② `enabledPlugins()` 冷启动必崩**（修复 **`e84667ec`**「plugin selection is result-typed (on-demand probe, cached, stage pinned at import) so a doc build can never crash the app」）：把「**构建文档的失败**」实现成**异常**，且描述缓存只在插件页动作里填充 ⇒ 冷启动后每次构建必崩；正确=**结果化**（`Ready`/`Blocked`）+ 原因可见 + 缓存初始化或按需解析（规则 3 + 5）；
      **③ 插件开关「关掉后自己变灰」**（修复 **`d01312cc`**「split run usability from control interactivity - the enable switch stays toggleable so a disabled plugin can always be re-enabled」；`PluginPresentation.kt:97` 的 `runUsable` 与 `:105` 的 `toggleable = true`）（用户原话：「插件关掉之后开关变灰无法再次开启，只有退出到其他界面才能开启」）：`PluginPresentation.kt:235` 的 `selectable = descriptor != null && errors.isEmpty()` 被当作 `PluginSettingsUI.kt:174` 的**控件可交互性** ⇒ 一关掉就没 descriptor ⇒ 开关自我禁用、用户无法自救；正确=**拆字段** `runUsable`（运行级门控）vs `toggleable`（已安装即 true），错误走**诊断行**（规则 1 + 2）；
      **规则要点**：一个投影字段只表达一个语义（跨用途必须另立字段）；控件不因「数据缺失」而不可交互；高频路径失败结果化不异常化；外部/历史数据容忍 + 诊断；缓存必须有明确填充时机。后续 **UI/插件相关批次一律按 §4.4 评审**。
- [~] **task-9 · 插件运行时接线（step 3a 完整落地：代码 + host/lint/NDK 0/0/0 + **真机门禁 PASS（五例）**；step 3b 未开始）**：
      **3a 形态**：组合根 `PluginHost::from_document(decoded, backend)`（**只登记**，无 dlopen/文件访问）→ **仅 43284** 在 bind 前 `open(WindowState::WaiterClosed)`（该 backend 全程无 PI waiter；**43499 绝不在 bind 前打开 —— R1：映射不得与 PI waiter 共存**）→ `production.bind(..., &plugin_host)` → **LKM 驻留窗口内** `POST_TERMINAL` 派发（`lkm_window.cpp:102`，经中性 **`PluginStageSink`** + `attach_plugin_stage()`；`execution_binding.cpp` 的 thunk 是唯一 `HostStage::` 调用点；**fail-soft，hook 失败绝不失败链**）→ pipeline 之后 `close()` → 诊断**仅 `registered() > 0` 时**打印（**无插件 ⇒ 零新增字节**，即无插件零字节回归判据）；
      **返工记录**：最初让窗口直接持有 `PluginHost*` → 5 个既有测试二进制被拖入 host 闭包、链接失败 ⇒ 改为**中性 sink**（函数指针 + ctx，同 `ChainOps`/`LkmTransport` 惯例），依赖只留在组合接缝（1 条 Makefile 规则），4 条既有规则不动；
      **真机门禁（PASS，`device-gates/plugin-runtime-3a-20261005-pass.md`）**：五例退出码全 0——正例 `called=1`/`calls=4`（真实链：插件 → `glk_contract_ops` → LkmProxy → `/dev/glk`）、负例 B `hook_failed=1` 链继续、负例 C `StageUnavailableOnBackend`（`calls=0`）、负例 D `HashMismatch`（未 dlopen）、**无插件零字节回归**（`run.plugin` 0 行）；事后 AVB 12/0、`/dev/glk` 不存在、LKM 自卸载、`Enforcing`；
      **门禁教训（设计已记）**：① `kmi` 不得手写（`KmiFieldMismatch`=`LkmPolicyError=5`；用 `--drop backend.cve_2026_43284 kmi` 派生），且**不得**把该码误判为 vermagic 不匹配；② 试验台清理必须含 `/data/local/tmp/.ghostlock_lkm_ok`（残留会让 `WaitResult` 误判 `LkmLoaded`、窗口 `open()` 失败而链仍 `EXIT=0`，**静默吃掉 `called`**）；
      **未覆盖**：**App 真实路径未复跑**（本门禁走 adb-only 试验台合成 SA，未走 App `IpSecManager`）——下一轮用 App 复跑；**环境前置**：43499 bootstrap 建试验台（同门禁 §驱动方式）；
      **观察（App 存档文档版本绑定过期）**：App 侧存档文档与 native 版本绑定；**任何 App 驱动的门禁都必须先用新 App 跑一次**（否则送入的是旧文档/旧字节），归档需记录 App 版本与文档来源；
      **同批文档**：UML §1（C4e/C6b/C7b）、§2.4（宿主生命周期 + **R1 禁止转换**）、§3.1（`PluginHost`/`PluginStageSink`/接缝关系）、§4.4（43284 + post_terminal 插件新时序）；设计 `plugin-runtime-integration-design.md` §11.6；AGENTS 措辞更新；
      **step 3b（43499 `pre_terminal`）**：**未开始**（打开点在 `steps.cpp:484-490`/`:517-521` anchor）。
- [~] **LOGGING · 43284 日志增强 + 插件日志能力位（A **已落地**；B 设计定稿**待放行**）**（契约 `contract-design.md` §3.14.7.9；用户需求）：
      **现状（已核实）**：**43284 全链 0 处 `pr_*`**——唯一可观测输出是 LKM 窗口诊断串（`lkm_window.cpp:17`）；用户参照上游 `V4bel/dirtyfrag`（`exp.c` **1952 行 / 63 处日志**，以错误路径与粗粒度进度为主），要求我们**展示更多**日志；插件侧 ABI 的日志入口与 host 实现**早已存在**（`glk_contract_abi.h:142` 的 `(*log)(ctx, level, msg)`、`plugin/host_ops.cpp:215`、`plugin/loader.cpp:162`），**缺的是能力位**：`glk_capability` 仅 7 位（`1<<0`…`1<<6`），探针 `host_caps` 不列 `log`（`probe.cpp:277-290`）；
      **批次 A（43284 日志）——已落地** `a04bdb5b`「batch A logging - bounded structured run.43284 lines across the whole chain, named failure reasons, keys withheld; 16 lines on the happy path」：载体 `backend/cve_2026_43284/diag_line.hpp` 的 `DiagLine`（固定缓冲/**无分配**/**无格式串**/值内控制字符归一 `_`/**绝不含密钥**），结构行 `run.43284 <phase> k=v`（单行 ≤256 B，超出截断追加 `truncated=1`），失败路径每条具名原因；**真机门禁 PASS**（`device-gates/43284-logging-20261005-pass.md`：五例退出码 0、正例 **16 行**、负例 B `hook_failed=1` 不中断、无插件回归 `run.plugin` 0 行且 43284 链日志逐行同形）；`lkm_window opened=…` 诊断串保留不动；**本批只做 43284，43499 另排**（43499 已有多处 `pr_*`，统一另批）；
      **批次 B（插件日志能力位）**：`GLK_CAP_LOG = 1u<<7`（**append-only，不 bump `GLK_ABI_VERSION`**）+ `capability_token()="log"` + 探针 `caps_list` 登记 + **`plugin-probe-golden.tsv` 重生成** + **63 份共享语料**新增 `log` accept/reject 用例 + Rust/Kotlin 词表同步（**产出端先行**，禁止消费端先行）；能力位存在的理由：插件要能在注册期 **fail-closed 检测**（`required_caps` 含 `log` 而 host 无 ⇒ `caps_rejected` + 注册拒绝）；
      **设计稿**：`docs/analysis/43284-logging-and-plugin-log-design.md`（随 `a04bdb5b` 提交）——契约 §3.14.7.9 已按其定稿（level `0..3` 越界按 1 记 `level_clamped`、msg ≤256 B、**64 条/模块/run + ≥1 ms/条**、前缀 `[countermeasure] <id> log(<level>): <msg>`、`log_calls`/`log_dropped` 记账、按模块栈上 ops 拷贝）；B 批门禁见该稿 §C/§D；
      **两项裁决已下（Lead 2026-10-05）**：① A 批 verbosity = **always-on** ✓（**不引入** `log_verbosity` wire 键，保持纯 backend；有界 ≤120 行/run、单行 ≤256 B；降噪另批）；② B 批配额 **64 条/模块/run、256 B/条、1 ms/条** ✓（超限丢弃并计 `log_dropped`，**不改控制流**）；**B 批待放行**；
      **门禁**：host / NDK 零告警 / lint 0 / Gradle 测试 + **真机日志断言**（43284 全链可按字段 grep：阶段进度、错误路径、`lkm_window opened=`）+ **探针 golden 重生成 + 语料对拍**（两侧 walker）+ **无插件零字节回归**不被破坏；
      **文档（已同步）**：契约 §3.14.7.9（A 已落地 + B 定稿待放行 + 两项裁决）、UML §3.1（`Capability` 词表含 `Log_1_7` 标「设计」；`DiagLine` 类与关系 = 批 A 已落地）、本条目。B 批落地时：golden **设备重采**、Rust walker 参考串（`plugin.rs:948` 等）与 Kotlin `PluginProbe` 词表 + `PluginProbeGoldenTest.kt:39` 同步。
- [x] **F4/F3/F5 结构修复**（`e6e534f`；门禁 `device-gates/s4-f4-f3-f5-20261005-pass.md`）：**F4**（高）token 表**导出 + 跨语言对拍**——`make -C src combination-manifest` 产 8 列 `combination-manifest.tsv`（`app/src/test/resources/` 对拍 + `profile-core/src/main/resources/` 运行时，两份逐字节一致，sha256 `d836a0d1…`），`combination_manifest_test` / `CombinationTokenAgreementTest` / `CombinationTokenHardcodeTest` 三处对拍，Kotlin 改为读资源、运行时零 token 字面量；**F3** 引入 `RouteKind::None`（= 4，不进 `kRouteCatalog`，3 个 route 对拍点零改动）、`Auto` 降为遗留解码（`[[deprecated]]`）；**F5** `terminal/root_child.hpp` 声明收尾搬入 `backend/cve_2026_43499/terminal/`（namespace `ghostlock::backend::cve_2026_43499::terminal`；防火墙 173 → 174，无新增越层边）。
- [x] **F1 · 组合维度分解 + F4 收尾**（`6111d74`/`0e9bc50`）：`PathKind{Rootchild=1,Shizuku=2,Umh=3}` + `CombinationId{backend,route,path}` 分解视图，`CombinationKind` 降为 wire 紧凑 id（uint8，枚举值与顺序不变）；`CombinationSpec` 增 `doc`/`path` 并重排字段消除 padding；40 行解析向量 `combination-resolve-vectors.tsv`（test-only 单副本）；Kotlin `CombinationKind.kt` 删除 → `CombinationCatalog.kt`（`CombinationSpec` + `CombinationCatalog` + UI 纯投影 `CombinationPresentation`；`resolve` 精确 / `normalize` 仅输入边界）+ v3 字段清单两副本断言。门禁联合全绿：host 174/4/4/0/0、NDK 0 告警、lint 0、Gradle 0（app 120 / profile-core 81）、cargo 45。
- [x] **R2b · CLI 最小化（选择只能来自 wire）**（`fa23331e` native + `ea6fb58c` Kotlin；门禁 `device-gates/s4-r2b-20261005-pass.md`）：CLI 只承载**传输 / 运行控制 / 安全 / 可观测**——保留 `--ghostlock-app-call`/`--load-prebuilt-profile`/`--enable-status-record`/`--dump-kernel-log`/`--force-attack`/`--probe-cve-2026-43284`；
      **删除** `--run-cve-2026-43284`/`--stage`/`--plugin`/`--cve43284-*`/`--allow-vermagic-rewrite`（死开关）及 `run_stage_cli`/`StagedRunOptions`/`run_staged`；`--allow-dev-target` 接到生产 carrier 校验（只放宽**绑定期**，链内仍 `ChainRejected`，A/B 见门禁 §3）；
      Kotlin dev 入口构造**同一份生产形态文档**走 app-call（`AppCallArgs` 与生产 argv 逐字一致断言）。未知参数 fail-closed。门禁：43284 生产 app-call `EXIT=0`、43499 `mcast_rootchild` **真冷启** `EXIT=0`、AVB 12/0；host 174/4/4/0/0、NDK 0 告警、lint 0、Gradle 0（app 122 / profile-core 82）、cargo 45。
- [x] **task-6 · 组件词汇导出 + 对拍**（native `bac185d3` 11 行 → `27fc50f9` 加 route 类 **14 行**；Kotlin `c684d899`）：`contract/identity.hpp` 的 backend/frontend/stepset/route 四类 kind 与 `kRouteCatalog` 导出为 `vocabulary-manifest.tsv`（两份逐字节一致；生成 `make -C src vocabulary-manifest`），Kotlin `VocabularyCatalog` 改为读资源、三个占位 backend 登记；对拍 `vocabulary_manifest_test.cpp` / `ComponentKindTest` / `VocabularyManifestAgreementTest`；AGENTS「双侧一致性」已登记该链。
- [x] **R4 已上移至 R2b 之前**（取证后修正顺序）。
- [ ] **R5（余项）**：**token 间接层清理**——共享任务板 **`task-10`**；文档同步部分（`PROFILE_SCHEMA(_ZH)` 全文重写 + README/AGENTS 更新）已完成：`06ec1129`（README 双语）/`735b8cbc`（AGENTS）/`9bd11aba`（schema 现状注）/**`07541f3`**（PROFILE_SCHEMA 全文重写，task-11）；R8 已独立完成，见下条；
- [x] **R8 · SHA-256 合一**（`657b3840`）：`plugin/sha256.*` 与 `backend/cve_2026_43284/ipsec/hmac_sha256.*` 合并为 `support/sha256.*`（删 4 增 2；防火墙 174 → 172），含**独立预言机对拍**、零行为变化。
- **待裁决**：**D1** 已由维护者回答（owner-qualified；43499 收进 backend{}，43284 独立段）；**D2** root 参数到内核侧传输（建议 LKM 读固定路径参数文件）—— **未决**
- 新增需求（计划 §6.2）：root 程序可选 + SELinux 恢复可选 → session.root.* 进 wire + Kotlin 高级设置 —— **未做**

#### CM 平台对策插件化

- [x] CM-1/CM-2/CM-3（ABI、loader/sha256、registry+controller）—— γ 批后落在 plugin/
- [ ] **CM-4 = R4c**：platform/vivo/** 删除 → 参考插件（vr_guard/vr_task_tag）+ ADR-0005；**真实 Vivo 设备门禁未完成**（本机 Sony），不得因 host 绿而标 supported
- [ ] **CM-5**：profile 加载清单/哈希字段 + assets 解包 + ABI 导出到 SDK 目录

#### 契约层剩余

- [~] **插件系统 = 外部 `.so` + 四投影契约**（**部分落地**：native C ABI / wire / Kotlin / extractor 四投影均已落地，仅 `RuntimeInfo` 与 CM-4 的 Vivo 门禁未完成；设计 §3.14.6，维护者 2026-10-05 明确）：
      - [x] **native C ABI 投影**（部分）：`glk_entry`/`glk_module` + `glk_contract_ops` 导出 + 窗口内同步派发（δ-4）
      - [x] **wire/HOCON 投影**（P1：`d951dd38`/`4d25d6e7`/`e5792ead`）：`plugin.<id>.{enabled,stage,module_path,module_hash,params.*,extract.*}` 全量落地（`plugin.<id>` 段形状 fail-closed；形状物证见下条）；**`plugin.conf` 资产已取消**（2026-10-05 裁决 → 覆盖存储），不再有「统一 plugin.conf」
      - [x] **Kotlin 投影（P1）**（`0ff79abd`/`4ecc87a6`/`cd2c6cd2`/`b3258bbc`/`5983b684`/`5957a373`/`b5caac75`；**投影字段语义按规范 §4.4**——`runUsable`/`toggleable`/`selected` 分离）：导入 `.so`（文件选择器 → 私有目录 → SHA-256 清单）→ **经 native 探针**读取自描述（不在 JVM 内 dlopen）→ 按 `params_schema` 渲染高级设置并做类型/必填/默认值**校验** → 生成 `plugin.<id>.*`（仅 enabled=true）；`b5caac75` 把 `extract.*` 带进 wire 并**闭合 P2 环**；真机 `device-gates/s4-p1-probe-20261005-pass.md`
      - [x] **extractor 投影（P2）**（`593e51de` + `940c404f` 兼容 5 键 header）：`--plugin-descriptor` 严格解析探针 TSV，R1/R3/R2 取值，产出 `plugin.<id>.extract.*`（仅 extract 片段）；`cargo test --release` **58 + 4 全绿**；文档 `1143f401`/`18ac3cb8`/`79b428c6`
      - [x] **native 探针**（`d951dd38`；`stage_availability` 于 `e5792ead`）：`--plugin-probe <path> [--expect-sha256]` 输出机器可读 7 列 TSV（Kotlin 校验、extractor 投影与 `PluginProbeGoldenTest` 共用同一份），header 5 键含按 backend 的阶段可用性
      - [ ] **RuntimeInfo**（含**当前 backend**）供插件按 backend 适配 —— **仍未做**（P3 之后）
      - [x] **P3 参考插件本体**（`940c404f`；**已移出** `1c70b3e4`）：独立项目 **`ghostlock-plugin-example`**（`include/` 内置 ABI 头 + `src/glk_probe_plugin.c` + `tools/probe_runner.cpp` + `build.sh android|host|abi-check` + 双语 README）——声明 **2 params + 1 extract**、`stage_mask = POST_TERMINAL`、caps `kernel_read,kernel_write`；原 aarch64 sha256 `5141c801…`，移出后**源码路径映射变化** → 新构建 sha256 `b7e4891d…`，设备复核 **EXIT=0**（门禁 `device-gates/s4-p3-reference-plugin-20261005-pass.md` 已加移出注，其余原始数据不回改）
      - [ ] **CM-4（P3 的 Vivo 目标）** = R4c：vivo → 插件；**真实 Vivo 设备门禁未完成**（本机 Sony），不得标 supported
- [x] **跨语言形状对拍物证**（`1b6c4709`）：`app/src/test/resources/plugin-wire-shape-golden.bin`（App 编码器产出、Kotlin 逐字节自断言；native `plugin_wire_golden_test` 断言 `parse` + `validate_plugin_wire` 接受同一字节流）。**该物证抓到一条真实缺陷**：中性 `profile::Value` 不保留 bool/uint 区分 → `enabled` 现按「非文本 + present + 值 ∈ {0,1}」校验（见 `1b6c4709`）
- [x] **`plugin.<id>` 段形状负例**（同批 `1b6c4709`）：定点字节手术构造 `plugin.<id>` 段 → **解码期 fail-closed**（owner 段白名单只认精确 `plugin`）
- [ ] **（小批）一致性修复 · 插件三端规则审计（2026-10-05）**：三端（`plugin/probe.cpp` 产出 / Kotlin / Rust）逐条约 **30 条规则中 24 条一致**，10 条分歧立项——**D1/D4/D5/D7 → native-core**、**D2/D3 → kotlin-app**、**共享语料 + 两侧 walker → extractor-rs**（语料 `app/src/test/resources/plugin-probe-conformance/{accept,reject}/*.tsv`：**目录即结论**、walker 须自证文件集合完备）；契约条目（header 放宽为 2 必填 + 3 可选、必需列 `-` 语义、退出码、行结束、D7 探针/注册期一致）已写入 `contract-design.md` §3.14.7.2/§3.14.7.7；
      **语料进展**：**63 份已落地**（`1a518cb9`：14 accept / 49 reject + 目录内 `manifest.tsv`，Rust walker 断言**双向完备** + 逐字节不变量，绿）；**Kotlin walker 待该例更正后落地**；并新增**语料取值域规则**（accept 取值必须在 ABI 声明域内，越界值只能进 `reject/`）
- [ ] **App 域对 /dev/glk 的可达性**：untrusted_app 对 null_device 的 ioctl 未验证（LKM 窗口在 app 域当前 opened=0，已被 fail-soft 兜住）

#### 其它

- [ ] **C**：KernelMemory 功能 + support/util.cpp 拆分（现为防火墙 4 条白名单的唯一成因）
- [ ] **（小）**43284 全链后的**例行**校验清单化（每次真机门禁附跑 avb-verify + files-check）并入门禁模板
- [ ] **（小）**清掉 3 处 __pycache__（已被 .gitignore 覆盖）
- [ ] **App 正常 profile 路径**最终确认：app 域直连已越过「设备事实」「终端探针」两处阻塞，**待维护者在 App 内点一次「一般执行 + 43284」确认全链**

### 0.4 阻塞 / 风险

| # | 项 | 状态 |
|---|---|---|
| 1 | **D2**（root 参数到内核侧的传输） | 未决 → 阻塞 R4 的 root 参数化部分 |
| 2 | **Vivo 真机门禁**（CM-4/R4c） | 本机非 Vivo，无法验证；host 绿 ≠ supported |
| 3 | KERNEL-PANIC-01 | 已知环境/时序问题；因果判定需同构建复现 + 冷机复跑 |
| 4 | 备份位置 | 必须在**仓库外**（../ghostlock-device-backup），不得落回 build/ |
| 5 | iCloud 同步 | 仓库在 ~/Documents 同步区；Gradle 输出已移出，**Make/NDK 输出仍在 build/（符号链接）** |

---

## 1. 分支目标

把 native 攻击 runtime 重塑为「顶级组件架构 + 新 backend/terminal 可插拔」，且**不破坏 43499 攻击路径**；
在此过程中接入 **CVE-2026-43284** 作为首个非 43499 backend，验证 ADR-0004 的中性性。

| 流 | 内容 | 权威文档 |
|---|---|---|
| S1 框架收敛 | 目录/契约/offset SSOT/platform·ancillary/去绑定 | top-level-architecture-rewrite-plan.md |
| S2 Steps/Terminal | Steps 可见、Backend<StepSet>、统一 terminal 接口、ActivationContext | terminal-steps-redesign.md（ADR-0004 R18–R21） |
| S3 43284 backend | 页缓存写 + LKM/UMH 终态、生产接线、可用性、app 域直连 | cve-2026-43284-backend-plan.md + 评估 + 改造计划 + 43284-production-adb-harness.md |
| S4 配置/wire 重设计 | 双 backend 下的 HOCON / kprofile / wire / profile 重构 | config-wire-redesign-plan.md（R0 计划；执行状态见 §0.3） |
| 契约层重设计 | 能力词汇/虚接口/ABI 归一/LKM 通道/插件活化 | contract-design.md |
| CM 平台对策插件化 | ABI + loader + registry + 参考插件 | countermeasure-plugin-plan.md（ADR-0005 待写） |
| 安全网 | 分区/文件基线、AVB/verity 校验、设备端 avbcheck | device-gates/ + tools/device-guard/ |

## 2. 文档地图（现行 vs 历史）

**现行（权威）**：adr/0001–0004（ADR-0005 待写）；执行顺序＝**本文件**；分册 top-level-architecture-rewrite-plan.md(S1)、
terminal-steps-redesign.md(S2)、cve-2026-43284-*.md(S3)、config-wire-redesign-plan.md(S4)、contract-design.md(契约层)、
countermeasure-plugin-plan.md(CM)、wire-transport-model.md(GLKv3 格式权威)、**docs/development/full-process-uml.md（全流程 UML 权威：IPO/状态机/Class/Sequence）**、s4-r6b-composition-design.md（组合 token / 维度分解）；
门禁 device-gates/*；规范 docs/development/*。

**历史/参考**：其余 docs/analysis/*；细粒度过程以 git 历史为准。

## 3. 分期进度（细节）

### S1 框架收敛
- [x] Phase 0 / A；A2-1、A2-2a–e、A2-3a–c；**A2-4 全部**；A2-5；身份词汇→contract（白名单 16→2）。
- [x] A3-1 AddressDiscovery；B 契约/自动化同步；**A3-2①②③**（③ 真机门禁 PASS）。
- [x] A 批 4：stage_types 拆分。
- [ ] **C**：KernelMemory 功能 + support/util.cpp 拆分（另 PR）。

### S2 Steps/Terminal
- [x] T0–T4（T4 真机 PASS）；T5 由 S3/B6 覆盖。

### S3 43284 backend
- [x] B0–B4；B5-1..B5-8；B5-9a–B5-9g；**B5-9h 全链 PASS（AVB 12/0）**。
- [x] B6/T5 生产接线（e6e7daa）+ 可用性翻转（869a43c）+ 生产修复（76b18c1/3be4622）。
- [x] 生产 app-call 门禁 PASS（adb 免 App 试验台）+ 修复 hook guard / 终点标记 / 试验台脚本权限。
- [x] **app 域直连修复**（3700b9e / eee7755 / 961e93e）：设备事实**降级**（degraded=proc_version,selinux,…）、
      umh_forward 就绪探针**域感知**（不可观测 ≠ 未就绪）、Kotlin 撤销「43284 强制 Shizuku」。
- [ ] App 正常 profile 路径最终确认（待维护者点一次）。
- [ ] App 域 LKM 窗口：/dev/glk 对 untrusted_app 不可开（fail-soft 兜住）；如需可用需单独标签决策。

### 契约层重设计（contract-design.md）
- [x] α（1e1dac8）、β（83d2df8，43499 真机 PASS）、γ（662a5f2）、δ-1/2/3（98b7c0c/d9c8ab7/f5077a8）、δ-4（7751550，端到端 PASS）。
- [x] 裁决：resident 默认常驻 + **会话绑定卸载**（fd 关＝窗口结束；定时器降为 60s 泄漏 watchdog）；插件调用**同步**；对策 **fail-soft**。
- [x] **插件自描述**（§3.14）：参数/提取 schema 与 `plugin.*` wire 大项已落地（P1+P2）；**`plugin.conf` 资产已取消**（2026-10-05 裁决 → 覆盖存储）；**`RuntimeInfo` 留待 P3**。

### S4 配置/wire 重设计
- [x] **R0**（计划）。
- [x] **R1**（`38f1343`）：schema 权威 + `SchemaRegistry` + `bind_all` default/required + `default_used` 诊断；**wire 零改动**；真机门禁 PASS（`device-gates/s4-r1-20261005-pass.md`）。
- [x] **R2**（`7bc6eb2`）、**R3**（`d6254c4`）、**R4**（`66ab498`）、**R6a**（`be7b58e`）、**R6b + T5**（`7169801`）、**F3/F5/F4**（`e6e534f`）、**F1 + F4 收尾**（`6111d74`/`0e9bc50`）、**R2b**（`fa23331e`/`ea6fb58c`）、**P1**（`d951dd38`/`4d25d6e7` + Kotlin 五批；真机门禁两条）、**P2**（`593e51de`；cargo 57+4 绿）——见 §0.3 与 `device-gates/`。
- [x] **P3 参考插件**（`940c404f`，**已移出**为独立项目 `ghostlock-plugin-example`，`1c70b3e4`）；[ ] **插件运行时接线（step 3）**（展开见 §0.3）。
- [ ] **PAYLOAD 三档**（设计定稿 + 用户已确认；实现待 native 半场；契约 §3.15）——见 §0.3。
- [ ] **R5**（余项；展开见 §0.3）。

### CM 平台对策插件化
- [x] CM-1/CM-2/CM-3；目录经 γ 批并入 plugin/。
- [ ] CM-4（=R4c，Vivo → 插件，Vivo 门禁未完成）、CM-5。

## 4. 判定标准（沿用）

- 普通改动：make -C src native-host-tests + NDK 零告警 + lint-tidy 0；跨语言改动另加 :app:testDebugUnitTest / :profile-core:test。
- **攻击关键路径**：**真机门禁**（冷机、KernelSU 未加载、固定 CPU 对）+ 门禁归档 —— **唯一权威判据**；cmp_disasm 为可选诊断。
- 新 profile / 新可用性未过真机**不得**标 supported。
