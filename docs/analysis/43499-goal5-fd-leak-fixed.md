# 43499 · 新目标第 5 轮：fd 泄漏已修（重试不再触顶）✓✓✓

## 一、根因（✓，一行常量 ✓）

```cpp
// memory/reclaim_plan.hpp:59
inline constexpr std::size_t kMaxReclaimSteps = **64U**;   ✗ 太小
// 真机计数（profile 来的 ✓）：pre=284 / spray=191 / post=32 ⇒ 计划需 **300+ 步** ✗ ≫ 64
// ⇒ plan_reclaim 返回 NoRoom ⇒ close_them_all **一个都不关** ✗ ⇒ fd 累积 ✓
⇒ 已改为 1024U ✓（注明：这是**实现容量**而非策略数字 ✓，附真机计数与现象出处 ✓）。
```

## 二、修后读数（✓，决定性 ✓）

```
修复前：`PFOPEN fd=` **512 → 5077 → 6083 → 7094 → 11668 → … → 23820 → 24813** ✗（设备上限 32768 ✓）
修复后：`PFOPEN fd=` **3 / 3 / 3 / 3 / 3 / 3 / 3 / 3** ✓✓ ⇒ **不再累积** ✓
⇒ ⇒ 约 7–8 轮触顶导致的 `discovery call-failed` / `victim-fork-failed` ✗ 随之消失 ✓：
   本窗口（180 s）内 `w2` 已跑到 round=5 且未见失败 ✓（修复前第 8 轮即死 ✗）。
```

## 三、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```

## 四、下一步（✓，后台跑已启动 ✓）

```
① 完整 15 轮 `w2`（后台 ✓）⇒ 判"是否有任一轮把受害者 root"✓（= 写是否概率性落地 ✓）；
② 若有 ⇒ 继续到 `w3` 与收尾 ✓；若无 ⇒ 回到"写为何不落"（对照退役 W2 的其余差异 ✓：
   `slab_drain` ✓ / `in_direct_map` ✓ / `discard_prebuilt_page` ✓）。
```
