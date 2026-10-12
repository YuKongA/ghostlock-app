# 43499 · 谜团解开：不是门，是我的测量口径（round 207）✓✓

## 打点证据（一次即定性 ✓）

```
新增打点（在门之前 ✓，撞门与命中两种跑都会打 ✓）：
  main: gate probe route=%s accepts=%d leaked=0x%llx base=0x%llx

sel1: route=select_stack accepts=**1** ✓ → main: page sent value=655 ✓
sel2: route=select_stack accepts=0 ✗
sel3: route=select_stack accepts=**1** ✓ → main: page sent value=655 ✓
sel4: route=select_stack accepts=**1** ✓ → main: page sent value=655 ✓
can : route=multicast_waiter accepts=0 ✗
⇒ **select 的通过率 3/4** ✓（比同期 canonical 更高 ✓）
```

## 更正（我此前的结论错在哪 ✗）

```
我一直在 grep `main: select route …` ✗ —— 那行只在**我分支的末尾**打印 ✓；
而真正"过了门"的跑打印的是 `main: page sent value=…` 等**别的行** ✓ ⇒ 被我判成"撞门" ✗；
⇒ ⇒ 所以此前所有"select 0/30 ✗"的读数**都是口径错误** ✗（不是代码问题 ✓）。
```

## 由此得到的新问题（精确 ✓）

```
select 的跑**过了门**（accepts=1 ✓）却**从未到达 `route_name == "select_stack"` 分支**（:894 ✗）；
⇒ 说明在 **:825 之后、:894 之前**有提前返回 ✗ ⇒ 下一批在 :883 附近再打一点即可定位 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
