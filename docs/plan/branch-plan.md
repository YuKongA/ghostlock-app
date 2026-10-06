

# 分支总 plan（vr-ko-bypass-dev）

> **唯一入口**：本文件回答「这个分支在做什么、现在到哪、下一步做什么、哪个文档算数」。
> 状态变化只改本文件的进度表；分册负责细节，不再各自维护全局顺序。
> 基线：93afccc。分支：vr-ko-bypass-dev。
> 最近更新：**2026-10-05**（S4：R0–R4、R6a、**R6b/T5**、**F1/F3/F4/F5**、**R2b**、**P1 插件接口**、**R8** 均已落地且各有真机门禁（`s4-f4-f3-f5-20261005-pass.md` / `s4-r2b-20261005-pass.md` / `s4-p1-probe-20261005-pass.md` / `s4-p1-plugin-wire-20261005-pass.md`）；43284 app 域直连打通；契约层 α–δ 完成；LKM 通道端到端 PASS；构建环境移出 iCloud）。
> **⏸ 冻结（用户指令 2026-10-05）**：插件工程与 payload/自定义 handoff **暂停并暂时禁用**；冻结期优先级 = **完善新 `cve_2026_43284` + 多 backend 选择机制**（两项 L 级设计待下发）——见文首「⏸ 冻结清单」。

## ⏸ 冻结清单（用户指令 2026-10-05）

> **用户方向性指令**：**暂停插件与自定义 handoff（payload）工程，先把架构做完**；冻结期优先级 = **完善新的 `cve_2026_43284` + 多 backend 选择机制**。

**冻结范围（暂停并暂时禁用）**

- **插件工程**：导入 / 校验 / **运行时接线**（含 **step 3b**、**extract spec 产出端**、插件日志能力位的**运行时使用**）；
- **`vr_guard` / `defex`：彻底删除，但**分两期**（用户裁决 2026-10-05 + `native-plugin` 审计）**：
  - **defex：已完成**（提交 **`a68e2d5a`**）；
  - **vr_guard (b) profile 面：已完成（`b55708a8`）**——`common.vr_guard` + `countermeasure.vivo_vr_guard.tracepoint_funcs`（含 wire 行与 manifest 行）删除 ⇒ **字段再无写入者 ⇒ `vr_guard_enabled()` 恒 false ⇒ `steps.cpp` 两处 `VivoPluginPolicies::apply(...)` 可证明 no-op**；**`countermeasure` owner 因此变空 ⇒ 一并移除**（白名单/manifest/AGENTS/UML）；**提交号待 native 步骤 ① 落地后回填**；
  - **vr_guard (a) 彻底删除 vivo 代码：已完成（`4a182217`；真机门禁 PASS 已归档）**——`platform/vivo/**`（8 文件 / 473 行）+ `steps.cpp:36-37` 的两处 include 与 `:194-213`/`:424-447` 的调用 + 2 个测试 + stub 属**攻击路径改动** ⇒ **必须真机门禁**（43499 链 PASS + 无插件零新增字节）——**已跑并通过**；**本批实现**：8 文件 + `platform_vivo_test.cpp` + `host/ancillary_stub.cpp` 删除、`steps.cpp` **−95 行**（两处 include、两个祖先块、两个只为它们存在的私有件）；**门禁数字**：host `EXIT=0`（告警 **9** 基线、**58 tests**、防火墙 **`174 files, 4/4/0/0`（182 → 174）**）· lint **0** · NDK **0**；**非行为差异**：`w2b` 的 run-state 标记随祖先块删除 ⇒ 日志 stage 轨迹少一项（`enter/complete("w2b")`），**行为无变化**；**沿革**：(b) `b55708a8` → (a) **`4a182217`**，两期不删历史。**真机门禁归档**：`docs/analysis/device-gates/vrguard-a-20261006/`。
    - **真机门禁结论（`vrguard-a-20261006`）**：`child is root!` → handoff `sent=1` → **`KernelSU ready`**，route `success=1`，**设备未重启**；**三条偏差**：① **第一次跑设备重启 = `KERNEL-PANIC-01` 一次性**（同构建冷机复跑即 PASS，勿据此归因代码）② **锁屏状态 ⇒ 日志落 `/data/local/tmp`**（不在 `Download/ghostlock-debug-log/`）③ **`w2b` 标记少一项 = 预期**（非行为差异，见 §3.1 注）。
- **payload / 自定义 handoff**：payload **执行半场**、handoff 设计稿推进、`payload.tier = "root"` 的 **wire 发射**与 **root 管理器 P1**；
- 下方相关条目均已标 **`⏸ 冻结（用户指令 2026-10-05）`**。

**冻结期要求**

1. **App**：隐藏插件与 payload 入口，且**不再发射** `plugin.*` / `payload.*`；
2. **native**：**解除插件宿主接线**（**字面注释掉**（不是开关）——**native 侧已提交 = `ca968a5a`**；**App 侧隐藏入口与停止发射在工作树、待提交**（提交后由 docs-uml 回填））；插件/payload 的 wire 校验路径与测试**保留不删**（**可逆**）；
3. **不得**继续推进：payload 执行半场、step 3b、extract spec 产出端、handoff 设计稿；
4. 已落地的实现与测试**保留**——本文的「已完成」项**不撤销**，只额外标冻结状态。

**恢复条件**：**新架构完成 + 用户放行**（两条同时满足）。

**冻结期优先级（新，两项均 L 级：先设计后实现）**

- **(a) HOCON 用 `available` 取代 `selection`（**形状已定稿**，用户 2026-10-05）**：用户原话「**存储的 hocon 里不要用 selection，用 available 定义，传入 kotlin 后在执行选择让用户选择可用的**」「**available 先选可用的 backend，available 的 backend 下面再有组合 token，terminal 删掉**」⇒ **两级 + 无 terminal**：
  ```hocon
  available {
    cve_2026_43499 = [ "mcast_rootchild", "pselect_rootchild" ]
    cve_2026_43284 = [ "umh" ]
  }
  ```
  **先选 `available` 的键（backend），再在其下选组合 token（值）**；**`selection { backend, terminal }` 全部删除**，**`terminal` 概念从 HOCON 移除**（token 已蕴含）；**运行时的选择仍写入 wire 的 `backend.<id>.steps`**。**用户已「赦免设计流程」，但实现前 Lead 只做一次极简确认**；实现分批待下发。
- **(b) `vr_guard` / `defex` 拆到 plugin 配置**：用户原话「**vr_guard 和 defex 应当拆分到 plugins 配置**」⇒ 从 `common.vr_guard` / `countermeasure.vivo_vr_guard.*` / `backend.cve_2026_43284.defex_symbol` 迁到 **`plugin` 系**的配置表达（**wire/manifest 迁移**）。
- **两项都等 Lead 与用户敲定口径后下发的设计稿**；本文件**暂不写实现细节**，只登记为冻结期的最高优先级工作项。

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
      **golden 基线替换（2026-10-06，已完成）**：**新基线 = `docs/analysis/device-gates/probe-resample-20261006-010653/`**（**原始 stdout 2470 B** + `ko.sha256`；提交 `45725ba2`）——与旧**摘要式**基线 `B5-9a-20261003-readonly-probe-pass.md` 的**替换关系与沿革**：旧基线只存档 11 行**摘要**，新基线存**原始 stdout 全文**（更完整：新增 `lkm_release parsed android=13 kmi=5015`、`ko_vermagic`/`req_vermagic` 原文）；**结论无实质差异**（同 release / 同 label / precheck `pass … match=1`）⇒ **以新基线为准，旧基线保留为沿革**。**实证（写进契约 §3.17）**：`ko_vermagic=5.15.202-android13-5.15.202_r00-dirty` 与设备 `5.15.189-…` **不同字**但 precheck 仍 **`match=1` / `ver_diff=None`** ⇒ **判定基于 KMI label（5015）而非逐字 vermagic** ⇒ 印证「**label 是交付身份、文件名跟随 label**」。
      **设计修正（2026-10-05 裁决）**：**`plugin.conf` 资产取消**——插件配置走既有覆盖存储，不加资产、不改 67 个资产 include；`ProfileLayout` 白名单接受 `plugin.<id>.*` 并 fail-closed（`contract-design.md` §3.14.7.5）；
      **过程偏差**：两处 F7 见上方条目（`7169801`、`b3258bbc`）；本批未改 wire 版本（仍 `schema == 3`），探针/绑定不在攻击关键路径。
- [x] ⏸ **冻结（用户指令 2026-10-05：不得继续推进 extract spec 产出端；已落地部分保留）**：**P2 · extractor 投影（`--plugin-descriptor`）**（`593e51de`；文档 `1143f401`/`18ac3cb8`/`79b428c6`）：extractor 新增 `--plugin-descriptor <probe-stdout.tsv>`（**可重复**、**仅 `--format conf`**）——严格解析探针 7 列 TSV → `PluginDescriptor`；取值按 **R1**（读回正在产出的 profile 字面量）/ **R3**（BTF `struct.<s>.<f>` 偏移或 `sizeof.<s>`）/ **R2**（kallsyms 符号，基址相对）；写入 `plugin { <id> { extract { … } } }`（**仅 `extract`**、可导入**片段**、追加在 `countermeasure` 之后、无条目时输出**逐字节不变**）；`required` 缺失 = 硬错误、`optional` 缺失 = 省略（**default 不顶替**）、声明类型与解析值矛盾 = 硬错误；HOCON 键简单 token 裸写、含点/特殊字符加引号（R1 的 owner-qualified 键始终加引号，与 App `HoconSupport.keyName` 一致）。
      **门禁**：`(cd tools/extract_rs && cargo test --release)` **57 + 4 全绿 / EXIT=0**；强制重建 **0 warning**。
      **待办**：① Kotlin 侧 `extract.*` 发射（闭合 P2 环，kotlin-app 在途）；② **P3 参考插件**（**已落地并移出为独立项目 `ghostlock-plugin-example`**，`940c404f`/`1c70b3e4`；本仓库只留 `tools/plugins/README.md` 指引）；③ **插件运行时接线 = step 3**（native-core step 2 的 host 之后；当前 pipeline 对插件宿主零引用）。
- [~] ⏸ **冻结（用户指令 2026-10-05）**：**PAYLOAD · 接管后三档 payload（**P1 分档 UI 已落地 + 本轮简化（用户决定）**；payload (b) 待 native 半场）**（设计 `docs/analysis/terminal-payload-tiers-design.md` r2 / `c335aabc`；契约 `contract-design.md` §3.15）：
      **P1 分档 UI（已落地）**：`b9aa31dc`（档位设置页：argv 契约 / 同意 / 执行前摘要 / 有界原子导入 + 两遍摘要校验 / 可重排 ko 列表）→ `fd507675`（新增 **`默认（不自定义）`** 作为初始档、页面骨架对齐）→ `809086e1`（改为**单选列表 + 选中行就地展开**，复用 `RadioButtonPreference`）→ `fa1d42d7`（本地化 check/run 串、动作式清除、单行脚注）；
      **本轮简化（用户决定 2026-10-05，契约 §3.15.2/§3.15.3 已记）**：**payload 页不再录入哈希**（「不要校验哈希，应当假设用户知道他们传入了什么」；`payload.*.sha256` 字段与 native「有则校验」语义**保留**，留给自动化/将来）、**无授权步骤**（「无需授权」；**执行前摘要保留为告知、不是门槛**）、**不再有独立检查栏**（运行期校验仍在，native 权威 + 运行前在运行按钮附近提示阻断原因）、**删除清除按钮**（切到 `默认（不自定义）` 即等价清空，只发射当前档）、档位文案改为 **`以内核权限执行脚本`** / **`向内核注入内核扩展`**（文案以 App 资源为准，自然中文不逐字直译）；**风险记录**：去掉哈希与授权 ⇒ **用户对 payload 内容自担责任**，native 仍 fail-closed 校验字段与路径并保留摘要作为可见告知；
      **payload (b)（待 native 半场）**：白名单 / manifest / 发射 / 持久化 / **(terminal × tier) 可用性矩阵**（native 导出、Kotlin 不硬编码）；
      **两条裁决（2026-10-05，契约 §3.15.2.1）**：① **manifest `required` 列 = 「无条件必需」**（物证：`schema.hpp` 的 **`kPayloadGlkv3Fields`** 仅 `payload.tier` 行为 `true`（按符号引用，不按行号）；**档内条件性由 `glkv3_parse.cpp` 的 `validate_payload_section` 保证，不得写进 `required` 列**）；② **索引键最终拼写 = `payload.ko.<i>.{path,sha256}`**（**改代码对齐文档**；**旧拼写 `payload.<i>.path` fail-closed 拒绝**；角括号占位符 `section` + `<占位符>` 后缀匹配与 `plugin.<id>.params.*` 同规，Kotlin `declarationFor()` 的索引路径分支留给 batch (b)）；**⏸ 状态：随 payload 冻结失效（沿革保留）**——① **对齐实现曾在工作树落地但未提交**（`kPayloadGlkv3Fields` 用 `ko.<i>.path`/`ko.<i>.sha256`、校验器要求 **`ko.` 前缀**并拒绝裸 `<i>.path`、两份 manifest 重生成且逐字节一致（工作树 sha256 `018804b363583612…`）；**已提交的 `74db3594` 是旧拼写那次**（扁平 `<i>.path`），**故此处暂不写提交号**——Lead 门禁过后按显式路径提交，我随后回填**提交号 + 承重 sha**；**剩余 Kotlin 侧**：`declarationFor()` 的索引路径分支 = batch (b)，Lead ping `kotlin-i18n` 重钉测试后我再补「Kotlin 已同步」； ② **冻结后 `payload` 段出现即拒（fail-closed，`ca968a5a`）** ⇒ **`ko.<i>` 对齐当前无运行时消费者**；③ **恢复＝撤销注释 + 跑门禁**（恢复清单见文首冻结清单与 `task-9`）；④ 原始裁决与拼写规则**保留不删**。
      **段**：新顶层 owner `payload`——`payload.tier` ∈ {`exec`,`script`,`ko`} + **单档互斥**；`exec.command`(argv)/`.sha256`、`script.path`/`.sha256`、`ko.count` 1..8 与 `ko.<i>.{path,sha256}`；**无 `payload.*` ⇒ 文档逐字节不变**；
      **安全边界**：相对 `<GHOSTLOCK_HOME>`、禁绝对/`..`/反斜杠/NUL + realpath 二次校验、≤256 B、可选哈希钉（**比对前不执行**）、先 `stat` 限长再读、`ko` **必须过 `lkm::precheck_module_file`**、**argv 不做 shell 拼接**、App 分档授权 + 可撤销 + **执行前摘要**；
      **失败语义**：**绝不中断攻击链**；用户显式请求未完成 ⇒ 本次运行结论标「**未完成**」+ `payload_error` / `ko[i]=<reason>`；**提权记录照记**；`ko` 逐项继续、不回滚；
      **可用性矩阵**（terminal × tier）**native 导出**、Kotlin 不硬编码、绑定期 fail-closed（沿用 `stage_availability` 纪律）；
      **依赖**：native 半场（wire 校验 + 绑定 + 接管后执行器；**先随 `root_child`**，出现第二个复用点再上提）× Kotlin 半场（layout/校验/分档设置页）；
      **门禁**：host / NDK 零告警 / lint 0 / Kotlin 测试 + **真机三档正例与负例**（`exec` rc=0；`script` 标记文件；2 个 ko 一好一坏 → `ko[1]=<reason>`、攻击链 PASS 但本次标「未完成」；sha 不符 / 路径含 `..` / 超限 / 矩阵外 → 拒绝且**不执行**）+ **无 payload 回归**（逐字节）+ AVB 12/0；结果按 `device-gates/` 归档；
      **结构同步（已同批完成）**：UML §1（C4c/C6c）、§3.1（`ghostlock__payload`）、§3.2（`data.payload`/`PayloadSettingsUI`）+ AGENTS 的 owner 白名单。
- [ ] ⏸ **部分解冻（2026-10-06）：handoff 已于 2026-10-06 解冻并纳入 `available`（D1–D4 已批准）；插件与 payload 仍冻结（用户指令 2026-10-05）**：**ROOT-MANAGER · 默认档 root 管理器选择（**设计已定稿（含制品来源）**；**UI 先行、wire 后行**——P1 的 App 侧（默认档+子菜单+检测+跳转）先落，`payload.tier = "root"`/`payload.root.*` 的 wire 发射排在 native 队列之后）**（设计 `docs/analysis/root-manager-selection-design.md`；契约 `contract-design.md` §3.15.8）：
      **定位**：归入 payload 轴——`payload.tier = "root"` + `payload.root.{kind, manager, argv}`（白名单、native 唯一权威、导出给 Kotlin）；**无 `payload` 段 = 现状 KernelSU/ksud 逐字节不变**，**显式 `kernelsu` ≡ 不写**；
      **现状事实**：`RootProgramKind`/`RootProgram` 已在 `contract/identity.hpp:366-380` 但 **wire 零键**；运行时硬编码 `$GHOSTLOCK_HOME/ksud`（`execution_binding.cpp:49-56`）；late-load 无 shell/无拼接（`lkm_image.hpp:85-92`）；包名权威 = KernelSU→`me.weishu.kernelsu`、其它→空（`schema.hpp:68-77`）；
      **外部事实（Lead 核实，引 URL）**：FolkPatch README 自述「non-parallel extended branch of **APatch**」（<https://github.com/LyraVoid/FolkPatch>），`app/build.gradle.kts` 实测 `applicationId=me.yuki.folk`/`namespace=me.bmax.apatch`；KernelSU-Next / SukiSU-Ultra / ReSukiSU 的 Gradle 路径核实尝试**均 404** ⇒ **分支包名不得猜，必须逐项核实**；
      **UI**：默认档展开为单选；**未实现者标「计划中」并置灰**（沿用既有模式，不假装可用）；矩阵由 native 导出、Kotlin 不硬编码；
      **一等检查（用户第三条，P1 必做）**：**管理器是否存在、是否可启动**——App 预检（包安装 / `ksud` 或指定路径可执行 / 哈希匹配）+ **native 绑定前复核**（权威在 native）；不存在/不可启动 ⇒ **不降级、不猜替代品**，结论标「未完成」+ 原因（不中断链路）；
      **P1（可落地）**：`kernelsu`（**KernelSU 及分支共享 `ksud`** ⇒ 默认按 ksud 走、行为与今天等价；`manager` **可选**包名，缺省用 `me.weishu.kernelsu`）+ `custom`（用户指定程序/argv，不猜包名）+ wire/白名单/UI 旋钮 + **存在性/可启动检查（App 预检 + native 复核）** + 穿到既有 late-load 路径；门禁 = host / NDK 零告警 / lint 0 / Kotlin 测试 + **真机 43284 无 payload 逐字节回归 + 显式 kernelsu 等价 + 43499 冷启回归** + AVB 12/0；
      **P2（计划中置灰）**：`folkpatch` = **加载 KernelPatch 模块**（官方文档 E4：`apd insmod` 手动重定位 + 绕过 modversions(CRC)/vermagic + `init_module` + **软重启生效**；前置条件 = SELinux Permissive + 已 Root，**正是 GhostLock 的 W1/W2 产物**）——**不复用** ksud late-load，需独立机制设计 + **独立真机门禁（含软重启后 Root 生效 / 重启后失效）**；**KernelSU 分支不再需要机制白名单**（共享 ksud，`manager` 可选手填）；
      **制品来源已结案（用户 2026-10-05，选项 a）**：**只从已安装的 FolkPatch 管理器 APK 提取内置预构建模块**——`packageManager.getApplicationInfo(pkg).sourceDir` → APK 内取模块 → **SHA-256** → **no-backup 不可变目录** → 路径+哈希交 native 并**复核**；**不允许用户自备**；**该轴不开任何文件导入 UI**；不可用（未安装 / APK 内无模块 / 提取失败 / 哈希不符）⇒ **置灰 + 具名原因**（fail-closed、不执行、不猜）；**实现要求**：提取与校验按此路径落地，且 **native 侧复核哈希**；
      **两条轴禁止重合（用户指出）**：「用户自备 `.ko`」一律走 **`payload.tier = ko`**（payload 轴），root 管理器轴只回答「**谁来接管 root**」、制品一律来自系统已安装的管理器；详见设计稿 §4.2；
      **依赖**：「**成功后自动跳转**」条目——跳转目标包名必须与本条目的 native 权威**同一映射**（`lkm_image.cpp:345-347`），不得出现第二个包名真相；P1 排在 **native 队列之后**；
      **用户问题状态**：①分支清单 / ②FolkPatch 机制 / ③**制品来源（已结案）** / ④`manager` 形态——**均已结案**；仅剩 **TODO-5：FolkPatch 内核兼容矩阵**（P2 设计任务，非用户问题）；
      **本批已同步**：UML §1（C6d）+ §3.1（`RootProgramKind`/`RootProgram` 与 payload 关系）、`contract-design.md` §3.15.8。**状态回填**：P1 放行时把「设计定稿待实现」改为实现状态（含依赖关系检查）。
- [~] ⏸ **冻结（用户指令 2026-10-05：**选择/检测**暂停；**跳转**部分已落地、保留）**：**ROOT-JUMP · 管理器选择 + 检测 + 跳转（跳转**已落地** `e500b407` + `4a02d19b`；选择/检测**在途**）**：**默认档 = 「启动 root 管理器」+ 子菜单**（「系统默认 KernelSU（默认）」= **不发射任何 payload 键**；或「检测到的其它受支持管理器」）；**未安装的不列出/置灰 + 具名原因**，不得让用户选不存在的目标；**白名单硬规则**：只允许有仓库/官方证据的包名——起点 `src/core/terminal/root_script.cpp:40-50`（`me.weishu.kernelsu.pr*`/`me.weishu.kernelsu-*`/`com.resukisu.resukisu*`/`com.kowx712.supermanager*`）+ `lkm_image.cpp` 的 `me.weishu.kernelsu`；Android 11+ 检测需 **`<queries>`**（manifest 已声明 5 项）；**未核实不得添加**；FolkPatch `me.yuki.folk` 属 P2；**UI 已可选 ≠ 已发射**（wire `payload.root.*` 属下一批 native）；**跳转部分** `app/src/main/kotlin/com/ghostlock/app/ui/RootManagerLaunch.kt` —— **App 侧唯一包名镜像** `RootManager`（`KernelSU("kernelsu", "me.weishu.kernelsu")`，注释直接指向 native 权威 `backend/cve_2026_43284/lkm/lkm_image.cpp:343-348` 的 `default_root_package()`；未知 ⇒ `null`，**绝不猜包名**；P1 加行时**同步 schema 行**）+ `RootManagerAction{Launch|Hint|Skip}` + 纯函数 `rootManagerAction(succeeded, rootProduced, packageName, launchable)`；`force_attack_test` 成功但不产生 root 状态 ⇒ **Skip**（不得误导），不可启动 ⇒ **Hint**（页面必须说明）；与 ROOT-MANAGER 条目**共用同一映射**；单测 `RootManagerLaunchTest`；**UML Class Kotlin（3.2）已同步**（类 + 关系 + 注）。
- [x] **规则溯源 · 三条真机缺陷 → 规范 §4.4（2026-10-05；提交号待 Lead 补）**：同一模式 24h 内连续三次真机缺陷，全部**已修**，规则已入 `docs/development/engineering-standards.md` **§4.4 状态/投影字段语义**（强制）：
      **① `recommend_shizuku` 升级即崩**（修复 **`9883a271`**「legacy validation drops unknown keys with a diagnostic instead of aborting」）：把「**旧版本遗留数据**的验证」写成 fail-closed 的 `require`（`ProfileLayout.validateLegacy`，`profile-core/.../ProfileLayout.kt:480`）⇒ 升级即崩；正确=**旧数据容忍 + 诊断**，只有本版本撰写的输入才 fail-closed（规则 4）；
      **② `enabledPlugins()` 冷启动必崩**（修复 **`e84667ec`**「plugin selection is result-typed (on-demand probe, cached, stage pinned at import) so a doc build can never crash the app」）：把「**构建文档的失败**」实现成**异常**，且描述缓存只在插件页动作里填充 ⇒ 冷启动后每次构建必崩；正确=**结果化**（`Ready`/`Blocked`）+ 原因可见 + 缓存初始化或按需解析（规则 3 + 5）；
      **③ 插件开关「关掉后自己变灰」**（修复 **`d01312cc`**「split run usability from control interactivity - the enable switch stays toggleable so a disabled plugin can always be re-enabled」；`PluginPresentation.kt:97` 的 `runUsable` 与 `:105` 的 `toggleable = true`）（用户原话：「插件关掉之后开关变灰无法再次开启，只有退出到其他界面才能开启」）：`PluginPresentation.kt:235` 的 `selectable = descriptor != null && errors.isEmpty()` 被当作 `PluginSettingsUI.kt:174` 的**控件可交互性** ⇒ 一关掉就没 descriptor ⇒ 开关自我禁用、用户无法自救；正确=**拆字段** `runUsable`（运行级门控）vs `toggleable`（已安装即 true），错误走**诊断行**（规则 1 + 2）；
      **规则要点**：一个投影字段只表达一个语义（跨用途必须另立字段）；控件不因「数据缺失」而不可交互；高频路径失败结果化不异常化；外部/历史数据容忍 + 诊断；缓存必须有明确填充时机。后续 **UI/插件相关批次一律按 §4.4 评审**。
- [~] ⏸ **冻结（用户指令 2026-10-05：native **字面注释掉**宿主接线——不是开关；代码/测试保留、恢复需撤销注释；**native 已提交 = `ca968a5a`**；**App 侧在工作树、待提交**）**：**task-9 · 插件运行时接线（step 3a 完整落地：代码 + host/lint/NDK 0/0/0 + **真机门禁 PASS（五例）**；step 3b 未开始）**：
      **3a 形态**：组合根 `PluginHost::from_document(decoded, backend)`（**只登记**，无 dlopen/文件访问）→ **仅 43284** 在 bind 前 `open(WindowState::WaiterClosed)`（该 backend 全程无 PI waiter；**43499 绝不在 bind 前打开 —— R1：映射不得与 PI waiter 共存**）→ `production.bind(..., &plugin_host)` → **LKM 驻留窗口内** `POST_TERMINAL` 派发（`lkm_window.cpp:102`，经中性 **`PluginStageSink`** + `attach_plugin_stage()`；`execution_binding.cpp` 的 thunk 是唯一 `HostStage::` 调用点；**fail-soft，hook 失败绝不失败链**）→ pipeline 之后 `close()` → 诊断**仅 `registered() > 0` 时**打印（**无插件 ⇒ 零新增字节**，即无插件零字节回归判据）；
      **返工记录**：最初让窗口直接持有 `PluginHost*` → 5 个既有测试二进制被拖入 host 闭包、链接失败 ⇒ 改为**中性 sink**（函数指针 + ctx，同 `ChainOps`/`LkmTransport` 惯例），依赖只留在组合接缝（1 条 Makefile 规则），4 条既有规则不动；
      **真机门禁（PASS，`device-gates/plugin-runtime-3a-20261005-pass.md`）**：五例退出码全 0——正例 `called=1`/`calls=4`（真实链：插件 → `glk_contract_ops` → LkmProxy → `/dev/glk`）、负例 B `hook_failed=1` 链继续、负例 C `StageUnavailableOnBackend`（`calls=0`）、负例 D `HashMismatch`（未 dlopen）、**无插件零字节回归**（`run.plugin` 0 行）；事后 AVB 12/0、`/dev/glk` 不存在、LKM 自卸载、`Enforcing`；
      **门禁教训（设计已记）**：① `kmi` 不得手写（`KmiFieldMismatch`=`LkmPolicyError=5`；用 `--drop backend.cve_2026_43284 kmi` 派生），且**不得**把该码误判为 vermagic 不匹配；② 试验台清理必须含 `/data/local/tmp/.ghostlock_lkm_ok`（残留会让 `WaitResult` 误判 `LkmLoaded`、窗口 `open()` 失败而链仍 `EXIT=0`，**静默吃掉 `called`**）；
      **未覆盖**：**App 真实路径未复跑**（本门禁走 adb-only 试验台合成 SA，未走 App `IpSecManager`）——下一轮用 App 复跑；**环境前置**：43499 bootstrap 建试验台（同门禁 §驱动方式）；
      **观察更正（2026-10-05，原「App 存档文档版本绑定过期」说法作废）**：G1 的失败**不是**文档/版本问题，而是**试验台载荷缺会话帧**——`--ghostlock-app-call` 的 stdin 必须是 **`[4B len][GLKv3 文档][会话帧]`**；用 `--enable-status-record` 时缺帧会在 `src/core/main.cpp:106-108` 首门被拒，且**错误只显示 `cannot load profile`**（极易误判）。**反证**：同一份 App 导出文档（1883 B）用 `--load-prebuilt-profile` **加载成功**（`resolved profile loaded: 5.15.189-…`，exit 0）；正解载荷 `[4B len][1883 B doc][88 B frame]` = **1975 B**。**基准缺口**：设备上 `/sdcard/Download/ghostlock-debug-log/` **不存在** ⇒ 无「App 实际送出字节」样本，**真实 App 路径仍待一次 App 运行**（试验台配方已写入 `docs/analysis/43284-production-adb-harness.md` §5 的硬要求）；
      **同批文档**：UML §1（C4e/C6b/C7b）、§2.4（宿主生命周期 + **R1 禁止转换**）、§3.1（`PluginHost`/`PluginStageSink`/接缝关系）、§4.4（43284 + post_terminal 插件新时序）；设计 `plugin-runtime-integration-design.md` §11.6；AGENTS 措辞更新；
      **step 3b（43499 `pre_terminal`）**：**未开始**（打开点在 `steps.cpp:484-490`/`:517-521` anchor）。
- [x] **43284 机制对齐 (b)**（`391ce7dd`「make the late-load command match the 43499 root script (release-derived --kmi label and --allow-shell), with a bare late-load fallback and a widened UMH argc bound」）：`build_late_load_command()` 现生成 **`--kmi <release 派生 label> --allow-shell`**（照 `root_script.cpp:184`；label 取 `selection.kmi->label`，**为空则回退裸 `late-load`**，不新增构建失败路径）；**未扩位掩码、未新增 wire 位**；跨层有界常量 **`terminal/umh_command.hpp` 的 `kUmhMaxArgc` 8 → 12**（最坏 9 参数 + NULL；`lkm_image.hpp` 转引）——**docs/AGENTS 当前未引用该数字**，后续若引用请按 **12** 写。
      **H1/H2 退役登记（2026-10-06）**：H1/H2 的**定义无处可寻** —— 全仓递归搜索（`docs/**`、`src/**`、ADR、工作树含 `build/` 与未跟踪、`~/.ghostlock/**`、兄弟目录）+ `git log -S/-G`（**无任何提交删除过含 H1 的行**）+ `git fsck --unreachable`（**无悬空 blob 含 H1**）+ 提交信息搜索，**皆无命中**；最早引用它们的 `b58a1d63`（**本会话之前**）本身就只写「H1/H2 device experiments still wait for the device」、**未写定义** ⇒ 用户确认非其所写并指示**退役该引用** ✓。**未执行**：43284 的 `ksud late-load` **真机判定未做** ✗；**已覆盖**：机制对齐 (b) 的代码面（`build_late_load_command()` 生成 `--kmi <release 派生 label> --allow-shell` + 空则回退裸 `late-load` + `kUmhMaxArgc` 8 → 12）由 host `-B` / lint / NDK 三绿覆盖 ✓。**更正前文错误**：先前「A301SO 是 5.15/43499 ⇒ 无法跑 43284」的判断**是错的** ✗ —— 设备 `index.conf` 报 `cve_2026_43284 usable = true` ✓（43284 三个 token 中 **`umh` 唯一可用** ✓）；日后若要补真机判定可直接执行（84B 会话帧布局见 `src/core/backend/cve_2026_43284/session_frame.hpp`：`version 1 / kind 1 / spi / encap_port / sender_port / icv_len 16 / aes_key[32] / hmac_key[32] / trailer 0x34383238`，只校验 version/kind/length/reserved/icv_len/trailer；现有 `run-gate.sh` **不发帧**，43284 缺帧为 fail-closed ⇒ 需先加一个小选项）。
- [~] ⏸ **部分冻结（用户指令 2026-10-05）：批 A = 43284 主线，**保持**；批 B 的**运行时使用**随插件工程冻结（能力位/测试保留）**：**LOGGING · 43284 日志增强 + 插件日志能力位（**A/B 均已落地**）**（契约 `contract-design.md` §3.14.7.9；用户需求）：
      **现状（已核实）**：**43284 全链 0 处 `pr_*`**——唯一可观测输出是 LKM 窗口诊断串（`lkm_window.cpp:17`）；用户参照上游 `V4bel/dirtyfrag`（`exp.c` **1952 行 / 63 处日志**，以错误路径与粗粒度进度为主），要求我们**展示更多**日志；插件侧 ABI 的日志入口与 host 实现**早已存在**（`glk_contract_abi.h:142` 的 `(*log)(ctx, level, msg)`、`plugin/host_ops.cpp:215`、`plugin/loader.cpp:162`），**缺的是能力位**：`glk_capability` 仅 7 位（`1<<0`…`1<<6`），探针 `host_caps` 不列 `log`（`probe.cpp:277-290`）；
      **批次 A（43284 日志）——已落地** `a04bdb5b`「batch A logging - bounded structured run.43284 lines across the whole chain, named failure reasons, keys withheld; 16 lines on the happy path」：载体 `backend/cve_2026_43284/diag_line.hpp` 的 `DiagLine`（固定缓冲/**无分配**/**无格式串**/值内控制字符归一 `_`/**绝不含密钥**），结构行 `run.43284 <phase> k=v`（单行 ≤256 B，超出截断追加 `truncated=1`），失败路径每条具名原因；**真机门禁 PASS**（`device-gates/43284-logging-20261005-pass.md`：五例退出码 0、正例 **16 行**、负例 B `hook_failed=1` 不中断、无插件回归 `run.plugin` 0 行且 43284 链日志逐行同形）；`lkm_window opened=…` 诊断串保留不动；**本批只做 43284，43499 另排**（43499 已有多处 `pr_*`，统一另批）；
      **批次 B（插件日志能力位）——产出端已落地** `2c9457fe`（`GLK_CAP_LOG = 1u<<7` + `Capability::Log` + **单一能力目录 `kCapabilityCatalog`**：`caps_list()` 遍历它，新位不会从 `host_caps` 静默消失；host 每模块栈上 ops 拷贝 + 前缀/截断/配额/限速 + `log_calls`/`log_dropped`）；**B 批已全线落地**：产出端 `2c9457fe`（唯一能力表 `kCapabilityCatalog[8]`）、**Rust `b7eb5f95`**（cap 词表接受 `log` + 示例插件优先 `ops->log`、**老 host 回退 stderr 同形前缀**；**刻意不声明 `GLK_CAP_LOG` 为必需**——该位语义是「要求 host 具备日志能力」，声明会让老 host 拒收；真机 golden 的 `plugin` 行 `required_caps` 仍为 `kernel_read,kernel_write`）、**Kotlin `fe66c793`**（`PluginProbe` = **token 透传** ⇒ 断言钉住「`log` 直通 + **未知未来 cap 也直通**」；fixture 第 4 行加 `log`；真机采集物**字节级复制**为第二输入 `plugin-probe-glk-probe-device.tsv`（sha `1a6e49d8…`）+ `PluginProbeDeviceCaptureTest`）；**确立边界**：**native 新增能力位不需要 Kotlin 改动**，Kotlin 只做透传与展示；**剩余仅两项设备侧待办（设备可用后）**：① 新示例插件产物 **`97c50d4d…`** 上机后**重采真机 golden**（模块哈希行更新）；② **三张 UI 截图**；原计划内容（**已按上述纠正**）：fixture 第 4 行加 `log`（格式增量，替代原「重生成」说法）+ **63 份共享语料不改**（输入子集仍合法）+ Rust/Kotlin 词表同步（**产出端先行**，禁止消费端先行）；能力位存在的理由：插件要能在注册期 **fail-closed 检测**（`required_caps` 含 `log` 而 host 无 ⇒ `caps_rejected` + 注册拒绝）；
      **设计稿**：`docs/archive/README.md（已归档索引 十三）`（随 `a04bdb5b` 提交）——契约 §3.14.7.9 已按其定稿（level `0..3` 越界按 1 记 `level_clamped`、msg ≤256 B、**64 条/模块/run + ≥1 ms/条**、前缀 `[countermeasure] <id> log(<level>): <msg>`、`log_calls`/`log_dropped` 记账、按模块栈上 ops 拷贝）；B 批门禁见该稿 §C/§D；
      **两项裁决已下（Lead 2026-10-05）**：① A 批 verbosity = **always-on** ✓（**不引入** `log_verbosity` wire 键，保持纯 backend；有界 ≤120 行/run、单行 ≤256 B；降噪另批）；② B 批配额 **64 条/模块/run、256 B/条、1 ms/条** ✓（超限丢弃并计 `log_dropped`，**不改控制流**）；**B 批待放行**；
      **门禁**：host / NDK 零告警 / lint 0 / Gradle 测试 + **真机日志断言**（43284 全链可按字段 grep：阶段进度、错误路径、`lkm_window opened=`）+ **fixture 第 4 行加 `log`（格式增量）+ 真机采集物归档 + 语料对拍**（两侧 walker）+ **无插件零字节回归**不被破坏；
      **文档（已同步）**：契约 §3.14.7.9（A 已落地 + B 定稿待放行 + 两项裁决）、UML §3.1（`Capability` 词表含 `Log_1_7` 标「设计」；`DiagLine` 类与关系 = 批 A 已落地）、本条目。B 批落地时：**fixture 第 4 行加 `log`**（Kotlin 侧；**身份行合成、不重采**）、**真机采集物归档为证据**、Rust walker 参考串（`plugin.rs:948` 等）与 Kotlin `PluginProbe` 词表 + `PluginProbeGoldenTest.kt:39` 同步。**物证职责边界**：golden = **跨端解析一致性**（身份行合成）；**真实制品一致性 = 真机采集物 + 门禁日志**，两者不可互替。
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

- [~] **CM-4 · 原计划「platform/vivo/** → 参考插件」已作废；`vr_guard` 按审计结论**分两期****：
      **改写的两个原因**：① 用户裁决「**vr_guard/defex 所有逻辑立刻完全删除**」与本条原计划「**迁到参考插件**」冲突，**以用户为准**；② `native-plugin` 审计显示 vr_guard **接在 43499 攻击链里**（`steps.cpp:36-37` include `platform/vivo/{registry,schema}.hpp`，`:194-213`/`:424-447` 两处 `VivoPluginPolicies::apply(...)`；读者 `contract/model.hpp:342-350`；绑定 `schema.hpp:99/144` + `glkv3_schema.hpp:38/93`），**删代码属攻击路径改动**，不能与 profile 面同批。
      **(b) profile 面（**已完成 `b55708a8`**）**：删 `common.vr_guard` + `countermeasure.vivo_vr_guard.tracepoint_funcs`（含 **wire 行与 manifest 行**）⇒ **再无写入者 ⇒ `vr_guard_enabled()` 恒 false ⇒ 两处调用可证明 no-op**（**不动攻击路径**）；**`countermeasure` owner 变空 ⇒ 一并移除**（白名单/manifest/AGENTS/UML）；**保留** `platform/vivo/**` 与两处调用（**惰性**）。
      **(a) 彻底删除 vivo 代码（已完成 `4a182217`；真机门禁 PASS 已归档）**：`platform/vivo/**`（**8 文件 / 473 行**）+ `steps.cpp` 两处 include 与调用 + 2 个测试 + stub ⇒ **攻击路径改动 ⇒ 必须真机门禁**（43499 链 PASS + 无插件零新增字节）——**已跑通过**；实现含 8 文件 + `platform_vivo_test.cpp` + `host/ancillary_stub.cpp` 删除、`steps.cpp` **−95 行**；门禁：host `EXIT=0`（告警 9 基线、58 tests、防火墙 `174 files, 4/4/0/0`（182 → 174））· lint 0 · NDK 0；**UML 已更新注记**（Class 图本无 `platform::vivo`/`VivoPluginPolicies` 节点可删；非行为差异：`w2b` stage 轨迹少一项）；**门禁归档**：`docs/analysis/device-gates/vrguard-a-20261006/`（`child is root!` → handoff `sent=1` → `KernelSU ready`，route `success=1`，设备未重启）。
- [ ] **CM-5**：profile 加载清单/哈希字段 + assets 解包 + ABI 导出到 SDK 目录

#### 契约层剩余

- [ ] **冻结期最高优先级 (a) · HOCON 用 `available` 取代 `selection`（**形状已定稿**；L 级实现分批待下发）**：**两级 + 无 terminal**——`available { cve_2026_43499 = [ "mcast_rootchild", "pselect_rootchild" ]; cve_2026_43284 = [ "umh" ] }`；**先选 backend（键）、再选其下 token（值）**；**`selection { backend, terminal }` 全部删除**、**`terminal` 概念从 HOCON 移除**（token 已蕴含）；**运行时选择仍写入 wire 的 `backend.<id>.steps`**；「**赦免设计流程**」，实现前 Lead 只做一次极简确认。
- [x] ~~**冻结期最高优先级 (b) · `vr_guard` / `defex` 拆到 plugin 配置**~~ **已被用户后续裁决取代（2026-10-05）：不迁移，改为删除**——defex 已完成（`a68e2d5a`）；vr_guard 按 §0.3 的 CM-4 **分两期**（(b) profile 面**已完成 `b55708a8`** / (a) 删代码**待设备门禁**）。本条目保留作**决策沿革**（原计划「迁到 plugin 配置」已作废）。
- [x] **LKM/DDK 构建里程碑（已完成，提交 `8187375c`）**：**源码修复** `tools/lkm/ghostlock/ghostlock.c`（sha256(12) `ec24903a3032`）——`+ #include <linux/version.h>`；`glk_lkm_addr_ok()` 内 `#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)` ⇒ `virt_addr_valid((const void *)(unsigned long)addr/last)`，`#else` 分支**文本逐字保留** ⇒ **旧 label 同 TU 同字节**；**语义等价**（u64/unsigned long/指针 LP64 同宽 + 已有 `len ≤ 4096` / `addr ≤ ULONG_MAX-len` 检查；6.6+ 宏展开 `__is_lm_address` 强转 u64、`virt_to_pfn(const void*)` 内部 `__pa()` 再强转回整数 ⇒ 同一数值同一条判定）。
      **复现 → 修复 → 全量**：改动前 `-PlkmLabel=android15-6.6` ⇒ `EXIT=1`（`incompatible integer to pointer conversion … memory.h:394 … :349 virt_to_pfn(const void *kaddr)`）；`./gradlew buildLkmImages`（**官方全 8 标签**）⇒ **EXIT=0**（8 行 `LKM <label> -> … (bytes)`）；`kmis.tsv` = **9 行**（表头 + 8）；`app/build.gradle.kts` mtime 全程不变（**无并发写者**）。
      **端到端**：`:app:assembleDebug` ⇒ **EXIT=0**；APK `GhostLock-v1.3(1160)-arm64-v8a-debug.apk`；`unzip -l | grep assets/lkm/` ⇒ **8 行**（**APK 资产含 8 个 `assets/lkm/<label>/ghostlock.ko`**）。
      **反证（`--rerun-tasks`）**：`mv` 走 `android17-6.18/ghostlock.ko` ⇒ **FAILED**「`missing 1 of 8 LKM images (all are listed): - android17-6.18 …`」；放回（sha 复核 `132f00f64a53…`）⇒ `--rerun-tasks` **BUILD SUCCESSFUL** ⇒ 撤回。
      **锚点对照（全量重跑后）**：12-5.10 `8e0c9f7d3754` 26512 · 13-5.10 `354c8043c820` 23920 · **13-5.15 `ac6681b71078` 23512 = 锚点 ✓** · 14-5.15 `8aee59911ec7` 29528 · 14-6.1 `d64a58811ae9` 25544 ⇒ **5 个旧 label 与改动前快照逐字节相同** ✓；新增 15-6.6 `4af87c29299c` 17032 · 16-6.12 `efc3211f90db` 17976 · 17-6.18 `132f00f64a53` 17688。
      **账本守卫**：`:app:verifyLkmLedger` ⇒ `EXIT=0` / `LKM ledger: 8 row(s) match the cache`；**证伪**：移走任一 `.ko` ⇒ `FALSIFY_EXIT=1` / `LKM ledger/cache mismatch: ledger lists […] but the cache holds […]` ⇒ 撤回复绿；**该守卫当场抓到过真实不一致**（单 label 跑把账本缩成 1 行而缓存有 5 个 ⇒ 修为「从**全量 manifest × 缓存**派生」）。**门禁**：`:app:testDebugUnitTest` **EXIT=0**，`^w:`=0、`^e:`=0。
      **未做项（明确挂账）**：① **`android15-6.6` / `16-6.12` / `17-6.18` 三个新 label 未做真机冒烟**（需设备；本次只保证**编译通过**与**判定等价**）② **运行时装载的设备端实跑未做**（`LkmImageProvisioner.provision` 目前仅**纯逻辑单测 4/4 绿**）。
- [ ] **执行组合子菜单 + general profile（L 级设计稿已起草，**待用户确认**）**：设计稿 = **`docs/analysis/execution-combination-menu-and-general-profile.md`**（含现状 file:line、8 条设计的「选项 + 代价 + 建议」、**9 个待确认点 U1–U9**、5 批落地顺序与门槛）。
      **要点**：① 执行组合改为**子菜单**（建议新页面）；② 路径**本地化**（`strings.xml` 新增 `execution_combo_*` 中/英键）；③ UI **只展示/可选 profile 声明的 `available`**（根因：当前可用性来自编译期目录——`CombinationPresentation.kt:26`、`GhostlockUI.kt:110`、`GhostlockViewModel.kt:1283/1289`）；④ **general profile**（`general.conf`：默认 43284、并入 5.15/6.1/6.6/6.12 空 43499 模版、**注释掉 43499 可用性声明**）；⑤ **未精确匹配 ⇒ 加载 general**（已核实 **native 不参与 release 匹配** ⇒ **纯 Kotlin 回退**，`AndroidProfileConfigController.kt:95/:742/:752`）；⑥ **`kernel_profile` → `profile`** 改名（143 处命中，独立批次）。
      **批次**：① 改名 → ② profile 资产 → ③ general 回退（**真机门禁**）→ ④ UI 子菜单 + 本地化（**截图**）→ ⑤ 可用性判据接线（**证伪实验**）；每批门槛见设计稿 §3。**实现前须用户确认 U1–U9**（尤其 U5「42384」是否为 `cve_2026_43284` 笔误、U4 general 是 1 份还是 8 份）。
- [ ] **兼容性政策：v1/v2/v3 矩阵（用户澄清 2026-10-06；契约 §3.21 已登记）**：**v3（未发布）⇒ 形状变更无需兼容**——旧 `selection{}`/`common{}`/`platform{}` **出现即拒、不加迁移**（这是 HOCON 重构与队列改造可自由改形状的依据）；**v1（旧 HOCON `schema_version = 1` 与旧 `offsets.json`）⇒ 经 Kotlin `LegacyProfileConverter`（唯一迁移点）转换**，native 不解析 v1；**v2（旧 bin）⇒ 当前拒绝**（依 `docs/analysis/s4-r2c-v3-only.md`「旧 bin 弃用」），**⚠ 待用户确认实际需求**——若确有需读的 v2 文件，另开 **Kotlin-only** 批次恢复导入/迁移，**native 仍 v3-only**。**边界**：兼容只在读入侧（App/Kotlin），native 永远 v3-only。
- [ ] **步骤队列取代 token（用户裁决 2026-10-05；设计已定稿 2026-10-06：`docs/archive/README.md（已归档设计稿索引）`，**v2.2 = `aec777a9`（定稿）**；沿革 v1 = `e150eb2b`、v2.1 = `2b6c3f13`）**：**实现分 M1–M5**（批次表见设计稿 §4.4；**契约 §3.20 已登记定稿规则**）。
      **决定与沿革**：profile/HOCON **不再选预烘焙 token，改为直接写【步骤队列】**（可读性优先，原则 2）；**两级保留**（先 backend、再其下 `queue`）；**不做**动态 DSL / 运行期自适应规划（**静态声明**）；旧「token 两级选择」被取代（**理由：HOCON 可读性**，沿革保留）；「每步 route」被取代（**理由：Route 整轮一次**，证据 `pipeline.hpp:53-62`）。
      **M1.1 · 已完成（`2760a599`）**：**归一化纯函数** `contract/step_plan.hpp`（**24 个 reason token 钉死**；**生产 0 接线**，供 M2 起复用与对拍）——属 M1 的补充分片（Lead 裁决）。
      **M1 · 已完成（`bcb94253`；与契约 §3.20 / 计划 M1–M5 / UML Class C++ 节点同批）**：`contract/step_catalog.hpp` 步骤词汇 + **编译期注册** + 目录↔执行体**折叠 `static_assert`** + 静默点改**硬失败（诊断优先）** + **全部 26 条守卫逐条证伪实验**（设计稿 §10.1：静默默认点 S1/S3/S5/S6/S8/S9/S13 + 新形状 S14a–S14d/S15/S16–S18 + §4.2 非法形状 11 行；每条给「造错点 → 失败输出 → 撤回核验」三件套，**证伪一律 `make -B`**）；**门禁**：host / NDK 零告警 / lint 0 **三绿 + 防火墙**；**无需真机门禁**（用户面与执行路径不变）。
      **M2 · 已完成（queue 承载片 = `4c20142f`；wire 对象数组 + 中立 `Document` 复合值 + 声明期/启动期 fail-closed + token 语法糖 + 同形对拍语料均已落地）**：wire 承载**对象数组** + **中立 `Document` 复合值**（**风险 R11：触及所有 owner 共用的绑定路径**）+ token **语法糖** + **声明期/启动期 fail-closed** + **同形对拍语料**（设计稿 §5-Q1）；**门禁**：三绿 + manifest 重生成 + 防火墙 + **逐条负例证伪** + 语料 ⇒ **必须真机门禁**，三条验收判据 = ① `supported` 计划**逐字节同路径**（43284 全链 + 43499 冷启）② **每个 `experimental` 各自门禁** ③ **无队列文档零新增字节**。 **canonical 分离（`queue_route`，canonical-only；几何 Map `route` 不被覆盖）+ 唯一映射点 `NativeProfile.backendSection()`（→ wire 键 `route`）+ 回显只认「与 `available` 声明逐值相等」均随本片落地**。
      **M3 · 已完成（`d34caa99` + `6a3d60c6`）**：**62 个资产**从 token 列表迁到**对象形态**（`route` 由 `CombinationCatalog` 派生、`queue` 由 native **`stepset-steps.tsv`** 展开、**零字面量**）；**E1 逐资产计划等价全绿**；**E3 字节清单 61 changed / 1 identical**；**真机 PASS**（归档 `device-gates/20261006-141317`；其间真机抓到 **`queue-and-token-both-present`** ⇒ 迁移不完整 ⇒ 修掉 token ✓）。

      **M4**：**UI 重做**（用户裁决 D4：旧 UI 不用了，**旧 UI 测试不作迁移验收**，替换/删除在实现批次登记）+ Kotlin 队列解析 + extractor `--format conf` 同形产出；**门禁**：`:profile-core:test :app:testDebugUnitTest` + 跨语言 golden 重生成 + `cargo test --release`。
      **M4 缺口已由 M5 修掉（`e59a8479`）**：`NativeProfileDocument.from()` 改为「**运行时载体优先、回退 `available.<id>` 声明**」⇒「HOCON 写队列发不上 wire」的缺口**已关闭**（连 `app/src/main/.../Profile.kt` 调用点）；**仍未做：M4(b) 设备端 app 侧装载实跑**（见下条边界）。**M2 完成判据（已达成）**：Kotlin golden（**3964 字符**）与 `make -C src glkv3-golden-hex` **逐字符相同**、两侧 fixture（native `--dump-fixture` **91 行**）逐字段对拍、**未声明三键 ⇒ 零新增字节**、**68 资产几何零变化**（`ProfileLayoutEquivalenceTest` ≥60 资产绿）、**5 条证伪**、`:profile-core:test :app:testDebugUnitTest --rerun-tasks` **EXIT=0**。
      **M4(b) 证据边界（2026-10-06，如实登记，不夸大）**：**① native 侧装载链（已设备证明 ✓）**——**LKM 的运行期装载由 native 完成并已在真机验证**：**M3 门禁 `device-gates/20261006-141317`** 与 **M5 门禁 `device-gates/20261006-152754`** 均到 **`KernelSU ready`**，而 KernelSU 启动**必须**由 native 装载 `helper.ko` ✓；**② App 侧打包（新证据，2026-10-06，已设备证明 ✓）**——debug APK 已装到 **A301SO**（`com.ghostlock.app`，versionName **1.3** ✓），**设备上 `pm path` 指向的 `base.apk` 内含 8 个 `assets/lkm/<label>/ghostlock.ko`**（`adb shell unzip -l` 计数 = **8** ✓）；**③ App 侧拷贝逻辑（仅单测覆盖）**——`LkmImageProvisioner.provision`（按设备 release 派生 label ⇒ 拷进 `<filesDir>/helper.ko`）**只有 4 条纯逻辑单测**（label 派生 / asset 路径 / 不重写判定 / 不匹配则拒绝回退 ✓）；**`<filesDir>/helper.ko` 在安装后为空属正常**（provisioning 在**开打时**发生 ✓）。**结论：已覆盖并保留边界**——**native 装载 + App 打包均已设备证明**；**App 侧拷贝仍为单测覆盖，未在设备上触发**。**若需端到端 App 路径** ⇒ **装机后在应用内触发一次**（或补 instrumentation 测试）。
      **M5 · 已完成（`e59a8479` + 归档 `af2feefc`）——单向门已关**：**HOCON 列表形态出现即拒**（两条独立具名诊断：`the token-list form was removed in M5; declare route+queue` / `empty token list is not a selection; declare route+queue`）；App **停发 wire `steps`**；**`from()` 改为「载体优先、回退 `available.<id>` 声明」**（修掉「导出 `.bin` 静默丢队列」✗）；native **具名拒 token**（`plan_error reason=token-form-removed path=backend.<id>.steps hint=declare-route-and-queue`）+ **归一化不再物化 token**；**golden 3920** / **`NO_SELECTION_HEX` 3766**（−44/−92 来源已写进注释）；两份 manifest **`62ea112b2caf`** 逐字节一致；门禁三连 + host/lint/NDK 全绿；**M5 真机 PASS**（二进制 `24e9accf84b9`）。**§11-U7**：**保留 `steps` 键 + 具名拒取值**（整键移除会退化成泛化 unknown-key ✗）。**legacy uint 边界**：`steps` 声明为 `WireKind::String` ⇒ legacy uint 路径**保留「就地改写为 token 文本」**（改写调用方已发来的键，**不是注入糖**）；不变量 = **queue 路径不得创建 `steps` 键**。
      **UML 分工**：**M1 已同批加 Class C++ 的 `StepSpec`/`StepCatalog`/`StepExecution` 节点与「目录↔执行体」绑定**（见本批提交信息）；**M2 落地时**才把 pipeline「编译期固定 → 注册表 + 校验」的结构改动画进 IPO/状态机（同批更新并写明图名）。
- [ ] **词汇重命名：`stepset` 轴的 `w1_w2`/`w1_w3` → `shizuku_rootchild`/`rootchild`（用户指令 2026-10-05）——**⏸ 已被 2026-10-05「队列取代 token」裁决吸收：任务取消**：
      **范围**：**只改 `stepset` 轴**——词汇 token `w1_w2`(id 1)/`w1_w3`(id 2) → **`shizuku_rootchild`/`rootchild`**；C++ `StepSetKind::W1W2`/`W1W3`（含 `W1W2Steps`/`W1W3Steps`、`ChainId::Cve43499W1W2/W1W3`、`Cve43499_W1W2/W1W3` 别名）随之改名；**数字 wire id 1/2 不变**、`pagecache_write`(3) 不动；`PathKind`/`FrontendKind`/`TerminalKind` **保持原样**；
      **语义**：`W1W2`＝跳过 seccomp 绕过（shell 入口/内核派生启动 ⇒ Shizuku 路径）；`W1W3`＝包含 seccomp 绕过（app 后代启动 ⇒ rootchild 路径）；
      **⏸ 挂起原因（硬证据，契约 §3.18）**：`kCombinationCatalog` 全 12 行显示 **stepset 与 path 不是 1:1**——43284 的**同一个 `pagecache_write` 对应三个 path**（`umh` / `rootchild` / `shizuku`）⇒ 用 path 名命名 stepset 会自相矛盾；43499 内 `w1_w3` 同时被 `*_rootchild` 与 `*_umh`（计划）使用 ⇒ 也只是近似重合。**命名定案前不动两侧实现**。
      **两轴正交**（契约 §3.18 已写清）：`stepset` = 跑哪些 W 阶段；`frontend`/`terminal` = 谁接管；
      **实现**：`native-hocon`（native）与 `kotlin-i18n`（Kotlin）**并行**，但**两侧均已按 Lead 指令挂起**（**⏸ 命名待用户确认**：用户已被告知 43284 的 `pagecache_write` **一 × 三 path** 事实；也可能保持原名/改用中性名如 `seccomp_bypass`）⇒ 状态「**待用户确认**」，定案后再实现并回填提交号；
      **门禁**：**不需真机门禁**（不是 profile/wire 字段；资产 0 命中）——host / NDK / lint / Gradle 测试 + 词汇 manifest 两副本对拍 + `vocabulary_manifest_test` / Kotlin `VocabularyCatalog` 对拍；
      **文档（本批已改）**：契约 **§3.18** + `ChainId` 注；UML §3.1 注；ADR-0004 与 `terminal-steps-redesign.md` 加重命名横幅（历史类型名保留）；
      **⏸ 结论（2026-10-05 后续裁决）**：**任务取消**——队列裁决后 **stepset 不再是用户选择面**（HOCON 写**步骤 id**），`w1_w2`/`w1_w3` 只余**内部预设/归一化名**；§3.18 已改标「被吸收」，ADR/设计稿的旧名继续按「当时记录」保留。
- [x] **HOCON 重构（形状定稿；**native ①②③④ 已落地，App 侧 ③ 已全绿并提交 `c443f5f0`**）**：**提交**：① 根级标量通道（`kernel_major`/`kernel_minor`/`safe_mode`）+ 删 `common`/`countermeasure` owner + **vr_guard (b)** = **`b55708a8`**；②③④ `platform.abi.*` → **`backend.cve_2026_43499.abi.*`**（62 处）+ 43284 执行项 → **`backend.cve_2026_43284.execution.*`** + **`wire_only`** 机制 + manifest **114 行** = **`23958eb0`**；
      **形状（用户裁决 2026-10-05）**：**根级标量** `schema_version` / `release` / **`kernel_major`** / **`kernel_minor`（新增）** / `safe_mode`（wire 与 profile 对齐，放在根）；**`common` owner 删除**；**`platform` owner 整体删除**（`platform.abi.{task_struct,cred,kernel,offset}.*` → **`backend.cve_2026_43499.abi.*`**）；**43284**：`steps` 留顶层（选择轴），`late_load_args`/`selinux_exec_context`/`module_poll_attempts`/`module_poll_interval_ms`/`wait_timeout_ms` → **`execution.*`**，**`kmi`/`lkm_path`/`carrier_path` 从 profile 删除**（**wire 字段保留、运行时现算注入**；`lkm_path`/`carrier_path`/各 `.ko` 路径统一在 **GhostLock 内部目录**解析）；**`selection { backend, terminal }` 删除** → **`available { <backend> = [ tokens ] }`（两级、无 terminal）**，运行时选择仍写 wire 的 `backend.<id>.steps`；**`index.conf` 的 `backends = [{ id, available }]` 改名 `usable`**（构建/资产层语义，与 profile 层 `available{}` 刻意不同名）；**extractor `--format conf` 同批产出新形状**（**已完成**：过渡层 `translate_conf_path` 删除 = **`a6241bc0`**，最终词汇 only）；**`profile-legacy/*.conf`（v1 夹具）保留旧形状**；
      **文档（已同批更新）**：契约新增 **§3.16**（owner 集与根级键白名单、逐条搬迁、消歧、与冻结的关系）；`PROFILE_SCHEMA.md` / `PROFILE_SCHEMA_ZH.md` **按新形状重写（双语同步）**；主模板 `docs/profile/PROFILE_TEMPLATE.conf` **重写**（含删除项与运行时注入说明）；UML（§1 IPO 的 profile 装配链 + §3.1 owner 结构注，**提交信息写明图名**）；
      **⏸ 不受影响**：插件 / payload 条目按文首冻结清单保持注释状态（出现即拒）；
      **门禁（实现侧）**：host / NDK 零告警 / lint 0 / Gradle 测试 + **manifest 重生成**（两份逐字节）+ **extractor `cargo test`** + 真机（43284 全链回归；`kmi` 现值由 native 注入，**profile 出现 `kmi` 即拒**的负例）；
      **状态**：**native 已定稿**（提交：① `b55708a8`、②③④ `23958eb0`；**extractor 已定稿 = `a6241bc0`**；**兼容性政策 = `613696af`**）（物证：manifest **114 行** = 10 头 + **104 字段**、两份逐字节一致 sha256 **`68bd7a506a210077`**、裸跑 `ok (104 fields, both copies)`；`kmi`/`lkm_path`/`carrier_path` **0 命中**；门禁 host `EXIT=0`（告警 9 基线、58 tests、防火墙 `180/4/4/0/0`）· lint 0 · NDK 0；**六条负例** `src/core/tests/profile_v3_test.cpp:195-242`）；**App 侧 ③（测试 + golden）已完成 = `c443f5f0`（全绿）** ⇒ 跨端记「**三侧已定稿**」（**extractor 同批产出新形状**）；
- [~] ⏸ **冻结（用户指令 2026-10-05：暂停插件工程；已落地实现/测试保留、可逆）**：**插件系统 = 外部 `.so` + 四投影契约**（**部分落地**：native C ABI / wire / Kotlin / extractor 四投影均已落地，仅 `RuntimeInfo` 与 CM-4 的 Vivo 门禁未完成；设计 §3.14.6，维护者 2026-10-05 明确）：
      - [x] **native C ABI 投影**（部分）：`glk_entry`/`glk_module` + `glk_contract_ops` 导出 + 窗口内同步派发（δ-4）
      - [x] **wire/HOCON 投影**（P1：`d951dd38`/`4d25d6e7`/`e5792ead`）：`plugin.<id>.{enabled,stage,module_path,module_hash,params.*,extract.*}` 全量落地（`plugin.<id>` 段形状 fail-closed；形状物证见下条）；**`plugin.conf` 资产已取消**（2026-10-05 裁决 → 覆盖存储），不再有「统一 plugin.conf」
      - [x] **Kotlin 投影（P1）**（`0ff79abd`/`4ecc87a6`/`cd2c6cd2`/`b3258bbc`/`5983b684`/`5957a373`/`b5caac75`；**投影字段语义按规范 §4.4**——`runUsable`/`toggleable`/`selected` 分离）：导入 `.so`（文件选择器 → 私有目录 → SHA-256 清单）→ **经 native 探针**读取自描述（不在 JVM 内 dlopen）→ 按 `params_schema` 渲染高级设置并做类型/必填/默认值**校验** → 生成 `plugin.<id>.*`（仅 enabled=true）；`b5caac75` 把 `extract.*` 带进 wire 并**闭合 P2 环**；真机 `device-gates/s4-p1-probe-20261005-pass.md`
      - [x] **extractor 投影（P2）**（`593e51de` + `940c404f` 兼容 5 键 header）：`--plugin-descriptor` 严格解析探针 TSV，R1/R3/R2 取值，产出 `plugin.<id>.extract.*`（仅 extract 片段）；`cargo test --release` **58 + 4 全绿**；文档 `1143f401`/`18ac3cb8`/`79b428c6`
      - [x] **native 探针**（`d951dd38`；`stage_availability` 于 `e5792ead`）：`--plugin-probe <path> [--expect-sha256]` 输出机器可读 7 列 TSV（Kotlin 校验、extractor 投影与 `PluginProbeGoldenTest` 共用同一份），header 5 键含按 backend 的阶段可用性
      - [ ] **RuntimeInfo**（含**当前 backend**）供插件按 backend 适配 —— **仍未做**（P3 之后）
      - [x] **P3 参考插件本体**（`940c404f`；**已移出** `1c70b3e4`）：独立项目 **`ghostlock-plugin-example`**（`include/` 内置 ABI 头 + `src/glk_probe_plugin.c` + `tools/probe_runner.cpp` + `build.sh android|host|abi-check` + 双语 README）——声明 **2 params + 1 extract**、`stage_mask = POST_TERMINAL`、caps `kernel_read,kernel_write`；原 aarch64 sha256 `5141c801…`，移出后**源码路径映射变化** → 新构建 sha256 `b7e4891d…`，设备复核 **EXIT=0**（门禁 `device-gates/s4-p3-reference-plugin-20261005-pass.md` 已加移出注，其余原始数据不回改）
      - [~] **CM-4（P3 的 Vivo 目标）——作废并**分两期**（详见 §0.3）**：**(b) profile 面已完成（`b55708a8`）**（`common.vr_guard` + `countermeasure.vivo_vr_guard.*` 的 wire/manifest 行）；**(a) 彻底删除 `platform/vivo/**`（8 文件/473 行）+ `steps.cpp` 两处调用属攻击路径 ⇒ 已完成 `4a182217` + 真机门禁 PASS**；**不再**「迁到参考插件」
- [x] **跨语言形状对拍物证**（`1b6c4709`）：`app/src/test/resources/plugin-wire-shape-golden.bin`（App 编码器产出、Kotlin 逐字节自断言；native `plugin_wire_golden_test` 断言 `parse` + `validate_plugin_wire` 接受同一字节流）。**该物证抓到一条真实缺陷**：中性 `profile::Value` 不保留 bool/uint 区分 → `enabled` 现按「非文本 + present + 值 ∈ {0,1}」校验（见 `1b6c4709`）
- [x] **`plugin.<id>` 段形状负例**（同批 `1b6c4709`）：定点字节手术构造 `plugin.<id>` 段 → **解码期 fail-closed**（owner 段白名单只认精确 `plugin`）
- [ ] **（小批）一致性修复 · 插件三端规则审计（2026-10-05）**：三端（`plugin/probe.cpp` 产出 / Kotlin / Rust）逐条约 **30 条规则中 24 条一致**，10 条分歧立项——**D1/D4/D5/D7 → native-core**、**D2/D3 → kotlin-app**、**共享语料 + 两侧 walker → extractor-rs**（语料 `app/src/test/resources/plugin-probe-conformance/{accept,reject}/*.tsv`：**目录即结论**、walker 须自证文件集合完备）；契约条目（header 放宽为 2 必填 + 3 可选、必需列 `-` 语义、退出码、行结束、D7 探针/注册期一致）已写入 `contract-design.md` §3.14.7.2/§3.14.7.7；
      **语料进展**：**63 份已落地**（`1a518cb9`：14 accept / 49 reject + 目录内 `manifest.tsv`，Rust walker 断言**双向完备** + 逐字节不变量，绿）；**Kotlin walker 待该例更正后落地**；并新增**语料取值域规则**（accept 取值必须在 ABI 声明域内，越界值只能进 `reject/`）
- [ ] **App 域对 /dev/glk 的可达性**：untrusted_app 对 null_device 的 ioctl 未验证（LKM 窗口在 app 域当前 opened=0，已被 fail-soft 兜住）

#### 其它

- [~] **C**：**`support/util.cpp` 拆分 = 已完成**（**R1 搬迁**：799 → **133 行**，只留 **12 个中性函数**；14 个函数 → 新 `backend/cve_2026_43499/spray.cpp`（728 行）+ `spray.hpp`（51 行）；9 处调用点改 `cve_2026_43499::spray::*`；`kernelsnitch.h` 唯一 TU = `spray.cpp`；**防火墙 `176 files, 0/0/0/0`（账本归零）**；**真机门禁 PASS** 归档 `device-gates/20261006-015621/`；**提交号待回填**）；**KernelMemory 功能仍待办**。
- [ ] **（小）**43284 全链后的**例行**校验清单化（每次真机门禁附跑 avb-verify + files-check）并入门禁模板
- [ ] **（小）**清掉 3 处 __pycache__（已被 .gitignore 覆盖）
- [ ] **App 正常 profile 路径**最终确认：app 域直连已越过「设备事实」「终端探针」两处阻塞，**待维护者在 App 内点一次「一般执行 + 43284」确认全链**

### 0.4 阻塞 / 风险

| # | 项 | 状态 |
|---|---|---|
| 1 | **D2**（root 参数到内核侧的传输） | 未决 → 阻塞 R4 的 root 参数化部分 |
| 2 | ~~**Vivo 真机门禁**（CM-4/R4c）~~ **已作废（用户裁决 2026-10-05）** | vivo VR guard 逻辑**彻底删除**，不再有 Vivo 目标与门禁 |
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
- [~] **C**：**`support/util.cpp` 拆分已完成**（R1 搬迁；防火墙账本归零 `176 files, 0/0/0/0`，见上一条）；**KernelMemory 功能**另 PR 待办。

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
- [x] ⏸ **冻结（用户指令 2026-10-05）**：**P3 参考插件**（`940c404f`，**已移出**为独立项目 `ghostlock-plugin-example`，`1c70b3e4`）；[ ] ⏸ **插件运行时接线（step 3）**（冻结）（展开见 §0.3）。
- [~] ⏸ **冻结（用户指令 2026-10-05）**：**PAYLOAD 三档**（**P1 分档 UI 已落地 + 本轮简化（用户决定）**；payload (b) 待 native 半场；契约 §3.15）——见 §0.3。
- [ ] **R5**（余项；展开见 §0.3）。

### CM 平台对策插件化
- [x] CM-1/CM-2/CM-3；目录经 γ 批并入 plugin/。
- [x] ~~CM-4（=R4c，Vivo → 插件）~~ **已作废（用户裁决 2026-10-05）：vivo VR guard 逻辑彻底删除**；[ ] CM-5。

## 4. 判定标准（沿用）

- 普通改动：make -C src native-host-tests + NDK 零告警 + lint-tidy 0；跨语言改动另加 :app:testDebugUnitTest / :profile-core:test。
- **攻击关键路径**：**真机门禁**（冷机、KernelSU 未加载、固定 CPU 对）+ 门禁归档 —— **唯一权威判据**；cmp_disasm 为可选诊断。
- 新 profile / 新可用性未过真机**不得**标 supported。

- **T2 冷启归档说明（2026-10-06）**：T2 的冷启由**合并提交 `7169801` 的那次门禁**承担；最接近的在册件 = **`docs/analysis/device-gates/gamma-20261005-43499-pass.md`**（`native-r1` 已确认：当时门禁跑的就是该合并树 ⇒ 结论仍有效）。
