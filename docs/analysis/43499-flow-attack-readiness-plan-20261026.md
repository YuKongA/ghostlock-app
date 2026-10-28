# 43499 Flow 推进到「可攻击」· 差距分析与批次建议

- 日期：2026-10-26
- 现状基线：工作树（HEAD 9dd7e34c + 未提交改动）；Flow 已收敛为 **Flow → 唯一 Cve43499Lifecycle → Native43499Jni**，`steps()` 为 10 个具名 step；Gate/readiness/UI 已就位。
- 结论：**当前不具备真实攻击能力**。Native 九入口已有物理实现，但 Kotlin 侧没有产生「合法物理输入」的能力，readiness 恒 NotReady，且环境探测为 stub；仿真器无法验证攻击效果。

## 1. 「可攻击」的验收定义

在真机（目标内核、KernelSU 未加载、冷机、固定 CPU 对、单 Primitive/route）上：

1. 选中 CVE-2026-43499 时 readiness=Ready，Run 页 Start 由 UI Gate 放行；
2. Flow 10 步全部 Done（failedStep 为空），receipts 齐全；
3. 收尾验收 `FinishVerdict.Accepted`：uid=0、脚本对象 SHA-256、ksud 证据、成功 marker==runId、SELinux 恢复、资源终结（released）；
4. 单 Primitive 的物理命中/写验证归档到 device-gates；未过不得标 supported、不得接默认 Flow。

## 2. 差距清单（必须先补的能力）

| # | 差距 | 现状证据 | 可复用来源 |
|---|---|---|---|
| G1 | Kotlin **物理输入生成**：landing.finalBytes、PrimitiveSpec（waiter/task/lock 最终地址 + frame bytes）、route 选择、placement/actor 参数 | `Cve43499PhysicalInputs` 全 null；无任何 Kotlin payload/address 计算代码 | 历史 K3 引擎（commit `7eaa1d1d`，2.0.4）：`engine/ktstep/KernelLayout.kt`(308L)、`DiscoverProcessor.kt`(70L)、`KernelRoute.kt`(26L)、`KernelPageWriter.kt`(33L)、`KernelFutexWords.kt`(116L) + K3 测试；C++ 参考 `legacy2/core/{memory,race,route_*}.cpp`、`very_legacy/temp_mcast_510.c`/`temp_select_510.c` |
| G2 | **profile→layout 数据源**：读取 `assets/profile/43499/*.conf`（72 个 + common），按 release/include/schema_version/route/offset 生成 KernelLayout | 当前 Kotlin 无 profile 解析（无 ConfigFactory 使用）；`profile-core` 已禁用；Native 按契约不读 profile | 历史 `data/profile/{Hocon,DeviceProfile,AndroidAssets}.kt` + `BuiltinProfileCatalog`；`com.typesafe:config` 依赖仍在 |
| G3 | **真实环境探测**：KernelSU/root/Shizuku 状态 | `rootAvailable()` 恒 true（KsuCli.kt:156）、`Natives.isManager=true`（Natives.kt:54）、KsuCli 为 noop | 需新实现（Shizuku binder 查询/SELinux/subprocess 探测）；readiness 需增加“设备已验证能力”维度 |
| G4 | **请求组装 & 页面输入**：route/CPU/timeout/attempts/profile 选择 → runSpec | `Cve43499RequestFactory` 的 physicalInputs 默认 null；43499 `configSchema` 为空；无 43499 配置 UI | 新接 backend config schema + UI；输入组装已在 `FinishHandoffAdapter.kt`（RequestFactory） |
| G5 | **攻击执行环境**：root 上下文执行收尾脚本、ksud/marker/SELinux 证据 | `FinishExecution` 已实现（uid==0 直跑 / su seam）；`FinishAcceptance` 已实现；但无真实 root 环境验证 | 现有代码 + 真机 |
| G6 | **分级验证** | 无 payload/地址 host 向量测试；未跑真机门禁 | 历史 K3 测试（KernelLayoutTest 4 / KernelCredentialTest 3 / KtStepEngineTest 9）+ C++ `payload_page_test` 退役向量 |
| G7 | **真机** | 目前只有 emulator-5554；仿真器内核不满足漏洞条件，不能验证攻击 | 需用户提供目标真机（型号/内核 release 与对应 profile） |

## 3. 关键约束

- 分层不变：**Flow（唯一编排，10 steps） → Cve43499Lifecycle（唯一生命周期层） → Native43499Jni**；payload/地址计算属**纯业务输入生成**（建议作为 Flow `request.validate` step 调用的纯 helper 模块，不新增编排层）。
- Native 不得读 profile / 推 route / 算最终地址 / 生成 payload（契约不变）；Kotlin/Lua 负责 route、地址、payload、顺序、重试。
- `legacy`/`legacy2`/`very_legacy` 只读参考；不得直接编译/依赖；只能 `git show`/阅读后在新模块重写。
- 门禁不变：未过真机不得标 supported、不得接默认 Flow/ksud；readiness 必须 fail-closed。

## 4. 建议批次顺序（每批单一目标）

| 批 | 内容 | 主要产出 | 验证 |
|---|---|---|---|
| P1（L 设计） | 43499 输入/布局/payload/route 设计：profile→KernelLayout→landing bytes→PrimitiveSpec→Flow 数据流；payload helper 位置；CBN | 设计文档 + design-critic 阻断 0 + 用户批准 | 文档评审 |
| P2 | 恢复/移植 K3 布局与 payload/地址**纯函数**（无 UI/无 Gate 改动） | `backend/cve43499/business/`（或 flow 内 helper）+ host 单测/黄金向量 | host 测试（KernelLayout/payload 向量，C++ 退役输入对拍） |
| P3 | Kotlin profile 解析（HOCON/资产）+ 设备匹配 + KernelLayout 映射 | profile→layout、device release 匹配、失效 fail-closed | host 单测 + emulator 读取资源 |
| P4 | 43499 config schema + 配置 UI（route/CPU/timeouts/attempts/profile）+ RequestFactory 组装完整 runSpec | UI 可表达输入、持久化、恢复 | emulator UI + 单测（请求形状） |
| P5 | 真实环境探测 + readiness 真实化 + finish/root 执行链 + Gate Ready 条件（device-verified 标记） | Ready 仅在“profile 匹配 + 输入可构造 + 设备已验证”时 | emulator 只验 UI；真机验 Ready |
| P6 | 真机门禁：先单 Primitive（建议 multicast_waiter）冷机跑通 uid=0/回写/marker/ksud/SELinux/资源终结；再扩 select/tcp、Lua 组合 | device-gate 记录、supported 状态更新 | 真机门禁（固定 CPU、冷机、KSU 未加载） |

## 5. 需要用户拍板的事项

1. **目标真机**：型号/内核 release（如 5.15.189-android13-…）与现有 `assets/profile/43499/*.conf` 是否匹配？没有真机则只能做到 P1–P4，无法验证“可攻击”。
2. **P1 是否批准开工**（L 级设计必须先评审；本批只出设计/数据流，不写攻击码）。
3. **Primitive 优先级**：建议 `multicast_waiter` 先（legacy 证据最全），select/tcp 后；TCP 仍需独立批准点。
4. **payload/地址计算位置**：建议 Flow `request.validate` step 调纯 helper（保持“编排只有 Flow”），而不是在 Native 或新增编排层；请确认。
5. **“可攻击”口径**：仅实验入口显式可用（Gate Ready 但默认 backend 仍不自动选），还是要求默认 Flow 可一键攻击？默认启用属 production 激活，需单独批准。

## 6. 停止/风险条件

- payload/地址错 → 43499 PI 路径可能导致内核 panic（已知概率性、非破坏事件，用户 2026-10-20 已接受 PI 例外）；真机门禁必须冷机、单 route、固定 CPU。
- profile 不匹配/字段缺失 ⇒ 必须 fail-closed（不入 ready、不构造请求）。
- dirty/indeterminate 闭包保留到进程退出；多次失败看 RSS/fd。
- 仿真器结果不构成攻击证据；host 单测只证明布局/向量正确。
