# 43499 真机第一跑：缺的是「装配字段」（2026-10-15）

## 真机逐行

```text
typed: assembled 43499 init=false refusal=init:bad-parameters        ← initialize 被拒（参数不合规）
typed: assembled 43499 write=false refusal=write:not-initialized     ← 连带（没初始化）
typed: assembled 43499 read=-1 same=false refusal=read:not-initialized
typed: assembled 43499 shutdown=false refusal=shutdown:not-initialized
```

## 解码（对照我读过的判定）

- `init:bad-parameters` 来自 `Access::initialize` 的第一道校验：`!params->usable_for_initialize()`；
- 43499 的 `usable_for_initialize()` 只看 `route != Unset`（我已传 route=1）—— 但它的注释写着
  「the assembly reads it, with a named refusal; complete when the assembly exists and its tests …」，
  并且同结构里有 `assembly_complete()`：**要求 fake_task / fake_lock / target / value 等装配字段非零** ✓；
- 我只填了 route/offset/offset_set ✗ ⇒ **装配字段全零 ⇒ 被拒** ✓（后续三步是级联，具名正确 ✓）。

## 因此 43499 的下一步很明确

1. 把这些**装配字段**按 route 填进参数：`fake_task` / `fake_lock` / `target` / `value`
   ＋ 各 route 自己的键与时延（`max_attempts` / `arm_hold` / `arm_sequence` / `park_ms` / `settle_us` /
   `consumer_delay_us` / `burst_calls` / `max_calls` / `fallback_lock_us` / `nice_modulus` / `policy`）；
2. 值从哪来：这些正是**档案里 per-route 的值** ✓（即「真实 profile 还需进一步设计」那块）；
   本轮先用常量（内置在 Kotlin，按你说的不进 profile ✗），真机验证链路是否通；
3. 填完重跑同一条最小序列，期望 `init=true` → `write`/`read` 给真值。

## 顺带

- 43499 这条链**不依赖 ESP/帧** ✓，所以它可以完全独立地推进到「能不能写/读」这一步；
- 具名机制（每一步都说清为什么）在这次真机跑里表现很好 —— 级联原因一眼可辨 ✓。
