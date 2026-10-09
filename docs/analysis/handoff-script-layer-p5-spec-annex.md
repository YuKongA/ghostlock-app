# 附件：P5 规格 · 取代关系与沿革（**从主件迁出**，2026-10-08）

> 分析类（≤8192 B）。**迁出理由** = 配额：主件 `handoff-script-layer-p5-spec.md` 原 8452 B > 8192 ⇒ 把 **§7「取代关系与沿革」**整节移到此（**内容未改**，仅位置迁移）。
> 主件：[handoff-script-layer-p5-spec.md](handoff-script-layer-p5-spec.md)（§7 处留一行指针）。

## §7 取代关系与沿革

- **本层取代**：`handoff-payload` 家族的**实现面**（计划/模板/执行器代码）· `src/core/plugin/**` 的**运行时接线**（构造/open/bind/close）· **`root_script` 生成器**（`src/core/terminal/root_script.{hpp,cpp}`，现为 `.disabled`）。
- **不取代**：既有**决策记录**（D1–D33）、`handoff-payload-*.md` 的设计结论、ADR-0008/0009/0010 —— 它们**改变角色**为**输入规格**（[rewrite-plan](../plan/rewrite-plan.md) §7）。
- **沿革（不删历史）**：token 白名单（—2026-10-06）→ queue（2026-10-06）→ **script（2026-10-08）**；插件冻结链 = P1 落地 → 冻结（2026-10-05）→ **解冻时按 D31 形态实现**（**不恢复旧 `.so` 单一通道**，D32）。
- **取代纪律**：本层的任何「兼容旧形态」提案（例如让 queue 复活作别名）**默认拒绝**（A2「取代而非叠加」；同一事实不得两处定义）。
