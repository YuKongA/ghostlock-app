# Kotlin AccessCapability 契约批次（2026-10-14）

状态：用户已批准「计划关系与影响文件」；本批仅落地 Kotlin 契约，不宣称任何 backend 已接通。

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
