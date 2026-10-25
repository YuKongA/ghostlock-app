# Kotlin AccessCapability 契约批次（2026-10-14）

状态：契约批次已完成；2026-10-19 追加 JNI 能力包装与生产入口，仍未执行真机门禁。

> **2026-10-23 取代**：本文的「能力视图 generation」条款被 [ADR-0023](adr/0023-access-capability-authority.md) 取代；能力视图改为无 generation/无 close 的纯借用标记，stale 由 owner/native 判。其余契约批次内容保留为历史。

## 计划关系

- 本批是 `jni-six-targets-params-plan.md` 的上层契约补充，不替代其中的六函数参数化、JNI 帧、native 生命周期或真机门禁。
- `contract-design.md` 的能力域划分仍是语义来源；backend 的具体读写限制只能由就绪会话的 native 描述符发布。
- 后续 JNI 适配、43499/43284 连接和各预留 backend 分别作为独立批次；本批不触及攻击窗口、现有 route 或 profile wire。

## 影响文件与行为差异

- 新增 `app/src/main/kotlin/com/ghostlock/app/core/{AccessCapability,AccessDescriptor,AccessResult,KernelMemoryAccess,PageCacheAccess,AccessSession,AccessSessionFactory}.kt`。
- 新增独立 Kotlin 类型与接口；没有修改既有 JNI 入口、C++、profile、UI 或现有运行路径。
- 内核虚拟地址与页缓存目标分型；读/写分别声明限制，缺席不推导为可用；写结果区分已确认、无效果和未知。
- Kotlin `AccessSession` 是生命周期权威，native 资源仍由 backend 持有；关闭后借用能力必须失效。当前只定义契约，尚无可运行的 session 实现。

## CBN 与扩展触点

| 决策点 | 方案 A：按地址域分型（采用） | 方案 B：每 backend 独立接口（更灵活） |
|---|---|---|
| 数字收益 | 现有 2 域只需 2 个能力接口；对照 2 个现有 + 4 个预留代号，最多少 4 个接口 | 各 backend 可有独立方法，不需域内约束抽象 |
| 初始工程成本 | 本批 7 个 Kotlin 契约文件 + 1 个测试文件 | 估计 6 个接口 + 共同结果/会话/工厂，至少 10 个文件 |
| 每次扩展边际成本 | 同域 backend：0 个核心接口改动；需新增适配器、描述符和测试 | 每 backend 至少新增 1 个接口 + 适配器 + 测试 |
| 翻转条件 | 新 backend 的目标不属于内核虚拟地址或文件页缓存，或连续 2 个 backend 的目标/值约束无法由描述符预筛 + native 重验表达 | 触发翻转时采用独立接口，不能扩宽已有接口的承诺 |

`OperationLimits.permits` 仅做宽度/对齐预筛；目标范围、值掩码、阶段依赖、实际调用次数、会话归属由每个 backend 的 native 实现重验。43499/43284 不得因共享接口而声称一般读写；64560/31431/43503/23274 仍按各自证据决定是否发布能力。扩展触点是 `AccessDescriptor` 构建、`AccessSessionFactory` backend 适配与对应拒绝测试，不是 route/primitive 共享。

**预估技术债**：当前只有接口，缺生命周期实现、JNI 描述符协议及 owner-token 复核；没有这些时不能发布就绪能力。偿还时机：第一个 JNI backend 适配批次开始前先完成状态/关闭/并发测试和协议版本化；未来新增地址域时重做 CBN 并更新本件 UML。

## 验证边界

- `:app:compileDebugKotlin` 证明契约可编译，不证明 JNI 适配、能力真实性、页缓存/内核写效果或 root。
- 未来适配批次需按 backend 的实际约束做拒绝测试、关闭/并发测试和设备门禁；未知结果不得折叠为成功。

| 层级 | 黑盒/白盒与极端输入 | 预期 |
|---|---|---|
| 离线白盒 | 宽度 0/8/16、偏移 0/1/MAX、可变集合在构造后更改、空 backend ID、零 verifiedBytes、请求帧二次消费 | 非法组合拒绝；快照不变；仅已确认写可获成功回执 |
| Kotlin 构建黑盒 | `:app:compileDebugKotlin` 退出码 | 0；契约源码编译 |
| JNI 适配批次白盒 | 未就绪/关闭/跨会话 target、并发 close、native 描述符不匹配、未知写结果 | 不越过 native 入口或返回具名拒绝；未知写不得成功 |
| 真机批次黑盒 | 单 backend、固定设备构建与 profile、冷机日志和实际写验证 | 仅设备证据可标 supported；本批不执行 |

## 2026-10-19 实现状态（JNI 能力包装批次）

本批完成“契约 → JNI 能力包装 → 生产入口”的第一条可编译链路。仍不跑真机。

### 已落地

- `data/nativeaccess/JniPageCacheOps.kt`：把 `PageCacheOps` 映射到既有四入口 JNI 会话（`Snapshot` / `ResolveTarget` / `ReadBlock` / `WriteBlock`）；宽度只认 native 发布的 16 字节，未解析到 token 时全部拒绝。
- `data/nativeaccess/JniCapabilitySession.kt`：在 `JniAccessSession` 之上发布 `AccessCapabilities`；能力只在 `initialize()` 成功后出现，未就绪不发布 page-cache 视图。
- `data/nativeaccess/JniCapabilitySessionFactory.kt`：按 backend token 打开能力会话；目标文件由调用方给出，不从内核版本推断。
- `engine/PageCacheEngine.kt`：生产入口打开同一个能力会话，读取描述符后实际调用 `targets()`、`read()`、`writeImage()`，再执行页缓存控制链；能力不可用时只降级为 warn，不伪造 ready。
- `ui/screen/run/RunCoordinator.kt`：不再直接调用临时 probe；只依赖 `PageCacheEngine`。
- `engine/EngineLog.kt`：info 无参数；warn/error 恰好一个 reason；debug/verbose 参数用 `\t` 分隔。

### 迁移

- `tempui/` 已移到 `engine/pagecache/`；`RunCoordinator` 不再 import `tempui`。
- 机械迁移不改移动文件的业务逻辑；控制面（hook/trigger/wait/LKM）仍留在页缓存链，只有数据面通过 `PageCacheAccess` 发布。

### 离线验证

```text
./gradlew :app:compileDebugKotlin          BUILD SUCCESSFUL
./gradlew :app:testDebugUnitTest           115 tests / 0 failures / 0 errors
```

新增测试：

- `EngineLogTest`：info 无参数；warn/error 一个 reason；debug 参数用 `\t`。
- `JniPageCacheOpsTest`：会话拒绝时 fail-closed，不发布 target、不读写。
- `PageCacheWriteImageTest`：`PageCacheAccess.writeImage` 按 16 字节块拆分并调用 `write`；非对齐镜像拒绝。
- `AssembledPageCacheAccess`：业务块读写与镜像写入的 PageCacheAccess 实现，底层仍复用同一个 Assembled43284 会话。

### 未完成 / 边界

- native `Snapshot` 目前只发布 phase/flags/width/generation，没有 target 路径列表；能力包装用调用方提供的 target 作为唯一目标，路径真实性仍由 backend 的 `ResolveTarget` 重验。
- 43284 业务数据面的 `Assembled43284.readBlock/writeBlock/writeImage` 已通过 `AssembledPageCacheAccess` 转成 `PageCacheAccess` 调用；控制面（hook/trigger/wait/LKM）仍留在页缓存链。
- 真机门禁未执行（按用户要求，生产级接线完成前不跑）。

### 2026-10-19 业务数据面收口

- `Assembled43284` 的 `readBlock` / `writeBlock` / `writeImage` 已改为 `AssembledPageCacheAccess` 的
  `PageCacheAccess` 调用；`readBlockNative` / `writeBlockNative` / `writeImageNative` 只作为内部实现。
- `AssembledPageCacheAccess` 复用同一个 accessId / sequence，不另开 native 会话。
- 通用 `JniCapabilitySession` 的 43284 路径仍缺 `resolve/read/write` 设备 ops；生产引擎不再用它做
  43284 预检，避免把 `resolve-unsupported` 当成数据面失败。
- 真机冒烟：`Page cache engine started` / `finished`；该轮攻击 `hook=0`（carrier/hook 状态），
  因此未产生模块写入，不能作为写效果证据。
