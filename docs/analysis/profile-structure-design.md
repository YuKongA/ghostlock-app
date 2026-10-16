# 真实 profile 的结构设计（L 级 · 待认可）

> 目标语境（2026-10-16 更新）：「真实 profile **结构**还需进一步设计，但**字段覆盖度较为完备**」。
> 本文只谈**结构**，不谈新字段；未获认可前不改码（AGENTS §2 L 级）。

## 1. 现状（实据）

- 单表 `profile-core/src/main/resources/profile-manifest-v3.tsv`，列为
  `owner<TAB>path<TAB>wire<TAB>required<TAB>default<TAB>source<TAB>doc<TAB>width`；width 是硬校验输入（越界即拒）。
- 顶层 owner 计数：`cve_2026_43499` 101 · `cve_2026_43284` 9 · `plugin` 6 · `root` 3。
- 路径形如 `backend.<owner>.execution.<field>`、`routes.<route>.<field>`、`heap.<field>`；
  另有 `plugin.<id>.params.*` / `plugin.<id>.extract.*` 的**动态**段（wire 为联合类型，未知成员 fail-closed）。
- Kotlin 侧 `NativeProfileDocument` 以 `valueAt(…)` 读值；`NativeProfile.kt` 已有 43499 的
  `heap.prepare_max_attempts`、`routes.select_stack.consumer_burst_calls`、`routes.multicast_waiter.arm_sequence/arm_hold` 等读取点。

## 2. 问题（为什么结构要再设计）

1. **读侧按名字散读**：装配处逐个 `valueAt("…")`，一步链路要跨 `heap.*`/`routes.*`/`stamp.*` 多处 ⇒ 阅读与审计成本高。
2. **链路阶段不可见**：profile 反映不出链的**阶段**（prepare → arm → write → wait → finish），而 Kotlin 装配恰恰按阶段走。
3. **多 route 的边界只靠前缀**：三条 route 的字段并列在 `routes.*` 下，**缺一个显式的 route 选择**（谁被选中、其余必须为默认）。
4. **无版本/兼容位**：v3 只有 schema 号；缺「哪些字段属于本地临时逻辑、哪些属发布面」的显式标记。

## 3. 建议结构（保持单表与既有校准，只加**分组语义**）

1. **按阶段分组**：把 43499 的字段归入 `stage.prepare.*` / `stage.arm.*` / `stage.write.*` / `stage.finish.*`，
   与 `steps/` 的阶段一一对应；**路径即阶段**，装配时一眼可查「这一步要哪些值」。
2. **显式 route 选择**：新增 `routes.selected`（枚举三选一）⇒ 未选中的 route 段必须全为默认值（可机器校验）。
3. **来源标记**：`source` 列已有（literal/profile）；建议再加一类 `temporary`（本地临时逻辑），
   并规定「发布面不得含 temporary」由门禁断言。
4. **域边界**：`root.*`（ksud/UMH 相关）与 `backend.*` 分离保持；`plugin.*` 动态段维持现有联合类型规则。
5. **读侧收敛**：Kotlin 只经**一个**按阶段聚合的访问器（如 `stageParams43499(document)`）取值，
   装配处不再散读 ⇒ 与「一处读取」一致。

## 4. 分工与红线

- profile 只提供**值**；选哪条 route、何时进入下一阶段、重试上限等**判断**仍在 Kotlin 装配。
- native 不读 profile（现状已如此）；结构变更不得把解析推给 native。

## 5. 迁移与验收

1. 结构变更走**同一张表**（加路径段），旧路径保留一版做别名并在同批登记删除时机。
2. 验收：`./gradlew checkAll` = 132/132 只增不减；`profile-core` 的单测（ProfileResolverTest / ProfileLayoutTest /
   PayloadCarriageTest / QueueWireCarriageTest）全绿；`nativeProfile` 导出与 native 合同比对无未知字段。
3. 结构定稿且实现后，再进 43499 的设备层实现（见 `43499-device-interface-extension.md`）。

## 6. 未决（待你裁定）

- 阶段分组用 `stage.*` 还是并进既有 `execution.*`（我倾向 `stage.*`：与 steps 同名，可机检）。
- `routes.selected` 用枚举还是位掩码（我倾向**枚举**：一条链只该走一条 route）。
- 旧路径别名保留多久（我倾向「结构定稿的下一批即删」，避免两份并存）。
