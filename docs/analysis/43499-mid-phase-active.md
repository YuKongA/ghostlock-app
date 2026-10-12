# 43499 · 中间 groom 接上（可选键）⇒ 完整率 1/3 → 2/3（round 144）

## 接法（按本树自己的"可选键"规矩 ✓）

```cpp
// main.cpp：prepare.inputs 已存在 ⇒ 只加一次**可选读取** ✓
const JsonNode *ps = doc->find("backend.cve_2026_43499.execution.heap.pre_shaped_groups");
if (ps != nullptr && doc->u64_of(*ps, v)) { prepare.inputs.pre_shaped_groups = v; }   // 没有就当 0 ✓
```

- **不**加进两张必需表 ✗（round 143 的教训：加了就被"值 0 ⇒ 具名拒绝"拦 ✓）；
- 映射表不碰这个字段 ⇒ 值不被覆盖 ✓；schema 目录里仍登记该键 ✓（人工权威 ✓）。

## 真机（3 跑 ✓）

```
run1: 3 × accepts=1 ✓   uid=2000 ✗
run2: 3 × accepts=1 ✓   uid=2000 ✗
run3: 1 × accepts=0 ✗   refused (payload-layout-not-accepted)
⇒ 完整率 2/3 ✓（接之前 1/3 ✗）⇒ 方向对 ✓，样本仍小 ✗
```

## 说明（不夸大 ✓）

- 3 跑 2 中，与 3 跑 1 中的差别**不足以定论** ✗；判据要继续累积（下一批跑更多次 ✓）；
- 奇偶机制已确定是"泄漏到哪个 mm_struct 的 bit16" ✓，中间组的规模是它的旋钮之一 ✓。

## 门禁

- android → RC=0，0 警 ✓；check → 108 PASS ✓；lint → 0 ✓。
