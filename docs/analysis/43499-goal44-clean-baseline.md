# 43499 · 第 44 轮：回退+守卫后的**干净基线**（问题的最简复现）✓✓

## 一、读数（✓✓，一次跑）

```
main: step result w1 => won ✓
main: step=w2 target=0xffffff8812194f98 matches=1 accepts=1 **value=0xffffff802abfd588** mode=2 ✓
main: step=w2 target=0xffffff880f03bd98 matches=1 accepts=1 **value=0xffffff802abfd588** mode=2 ✓
main: step=w2 target=0xffffff896a6d3d98 matches=1 accepts=1 **value=0xffffff802abfd588** mode=2 ✓
target-outside-direct-map 触发 **1** 次 ✓（守卫在工作 ✓）
main: route status=0/0 attempts=16 calls=1 **successes=1** ✓（与归档成功那次同 ✓）
main: victim uid=**2000** ✗
```

## 二、这条基线的意义（✓✓）

```
机制侧**全部正确** ✓：写值 = init_cred 别名（非零、恒定 ✓）/ 目标同窗 ✓ / 页被采纳 ✓ / 路由 successes=1 ✓；
而**效果侧为零** ✗（uid 恒 2000 ✓）⇒ ⇒ 这正是"**机制全对、效果不现**"的最干净复现 ✓，
也是本项目**唯一剩下的问题** ✓ —— 不再有"参数/臂/写值/守卫"层面的疑点 ✗（那些都已逐条对齐 ✓）。
```

## 三、下一步（✓，唯一可行的一步 ✓）

```
**独立读回**：跑完一轮后，用同一条别名路径**读**目标字（或读受害者的 cred 字段 ✓），
判"内核到底有没有把值写下去" ✓：
  · 若**写下了** ✗ ⇒ 问题在"受害者为何不据新 cred 报 uid" ✓（⇒ 查 cred 的引用/生命周期 ✓）；
  · 若**没写下** ✗ ⇒ 问题在"擦除式写入本身" ✓（⇒ 回到 route 的触发与元组 ✓，但机制读数已全对 ✓）。
性质：新组件（别名读 ✓）⇒ **L 级** ⇒ 按守则 §2 先出计划 ✓；本页即该计划的**动机与判据** ✓。
```

## 四、门禁（✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
