# 43499 · `select_stack` 移植物料就位（round 165，惰性 ✓）

## 已就位（不碰任何执行路径 ✓）

| 物料 | 位置 |
| --- | --- |
| 夹具 | `core/fixtures/wire-43499-select-stack.{json,framed}` ✓（照 collide-F 复制，仅把 `available.cve_2026_43499.route` 改成 `select_stack` ✓） |
| 移植研究 | `docs/analysis/43499-select-stack-study.md` ✓（机制四条 + 结构地图 + 差异 ✓） |
| 移植计划 | `docs/analysis/43499-select-stack-port-plan.md` ✓（5 项 + 三级判据 ✓） |
| 退役快照 | `docs/analysis/cve-2026-43499-select-stack-route.cpp` ✓（640 行 ✓） |

## 移植前的对照锚（真机 ✓）

```
连跑 5 次（同夹具 ✓）：
  run1/2/4：main: refused (payload-layout-not-accepted)   ← 奇偶先拦（与 route 无关 ✓）
  run3/5 ：main: refused (route-not-ported:select_stack)  ← ★ 移植前的锚 ✓
⇒ 即：**当前该 route 具名"未移植"** ✓ ⇒ 移植后这行应变成本 route 的正常报告 ✓
```

## 注意（顺序已实测 ✓）

- 页面的奇偶校验发生在**路由选择之前** ✗ ⇒ 所以"未移植"那句只在奇偶恰好合格时才出现 ✓（5 跑 2 次 ✓）；
  ⇒ 移植后的判据不能只看一次跑 ✓，要按**多次**统计（与 canonical 链同一做法 ✓）。

## 门禁

- `check` → 108 PASS ✓；`android` → 0 警 ✓；`lint` → 0 ✓；新增物料均为**数据/文档** ✓（无执行路径改动 ✓）。
