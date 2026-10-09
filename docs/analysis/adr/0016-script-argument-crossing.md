# ADR-0016：脚本参数如何过界（Str 一类的归约）

- 状态：**Proposed**（待用户批准）
- 日期：2026-10-09
- 索引：[README.md](README.md)
- 相关：**ADR-0009**（能力面硬线）· **ADR-0010**（句柄与资源生命周期）· **ADR-0011**（执行顺序权威）· **ADR-0014**（只暴露 kernel memory）

## 背景

面上七行（`contract/script_surface.hpp` 的 `kSurface[]`）的参数由 `ArgSpec{key, max, required}` 描述，**规格里没有类型位**；类型由每次调用以 `ArgView` 携带，取值 `U64` / `Str` / `Bool` / `Handle`（`script/call_sink.hpp`）。

而计划侧的参数是 `pipeline::PlanParam{const char *key; std::uint64_t value;}` —— **只有 u64**（`pipeline/plan_gate.hpp:80`）。

于是四类值里有两类装不进去。实测各类的现状：

| 类别 | 归约 | 现状 |
| --- | --- | --- |
| `U64` | 原值 | 无争议 |
| `Bool` | 0 或 1 | 无争议 |
| `Handle`（`{kind, slot, generation}`） | **`slot`**（有界 64 的序号） | **零件现成**：`script/handle_table.hpp` 的 `acquire` 给出 slot，失效时已有具名的 `HandleRefusal` |
| **`Str`** | **未定义** | 本 ADR 要回答的唯一问题 |

## 决策

**`Str` 一律归约为固定表内的一项，查不到即具名拒绝。** 理由与三条既有约束对齐：

1. **有界** —— 表长是常量，脚本无法让它长大（与 `kMaxHandles = 64`、`kStagedArenaBytes = 4096` 同一手法）；
2. **无文本比较** —— 判定用查表，不做字符串比较（与本仓既有的注入名处理一致）；
3. **拒绝有名** —— 查不到时给出具名拒绝码，而不是静默变成 0。

**被否决的两个选项**：

- **面上禁用 `Str`** —— 要改面（`ArgSpec` 增加类型位或改七行内容），而面是编译期断言约束的，改动面大于本问题；
- **把字符串哈希成 u64** —— 值域撞车时无法诊断，且失去「查不到即拒绝」的能力。

## 影响

- `script/` 侧：新增一张固定名字表与一个查表函数（有界、无分配）；
- 归约点只有一处：把 `CallView` 转成 plan 项的那一步（ADR-0010 已界定句柄；此处只补 `Str`）；
- 不触及 `contract/script_surface.hpp` 的七行与它的编译期判据。

## 未决

「表里放哪些名字」由谁来定：候选是**面上已有的种类名**（`target_kind` / `src_kind` / `dst_kind` 一处一个）与 `terminal` 名。这一条我**不擅自定**，随本 ADR 一并待批准。
