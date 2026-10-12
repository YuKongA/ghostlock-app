# 43499 · 第 113 轮：`setup_settle_us` 5000 实验（无变化 ⇒ 不单独决定）✓

## 一、实验（✓，数据层、可回退 ✓）

```
把夹具 `…execution.race.setup_settle_us` 由 **50000 → 5000** ✓（= 退役 `fast_repair` 时的值 ✓），
重打 framed ✓ 推送 ✓ 跑 ✓ ⇒ ⇒ **⇒ 读数无变化** ✗：
  `main: route status=0/0 clean=1/1 attempts=16 calls=1 successes=1 stamp=-1/99` ✓（同前 ✓）；
  `main: step result w1 => won` ✓；`main: victim uid=2000` ✗；`uid=0` 计数 **0** ✗。
```

## 二、结论（✓✓）

```
⇒ ⇒ **⇒ settle 取 5000 还是 50000，不单独决定"落不落"** ✗（⇒ 至少不是充分条件 ✓）；
⇒ 已**恢复为 50000** ✓（与归档一致 ✓，第 90 轮核过 ✓）；门禁复绿 ✓（check ✓ / lint ✓）。
⇒ ⇒ 含义：第 107/108 轮补的那段 settle **属于"补齐相位"** ✓（⇒ 该补 ✓），而**不是** `uid=0` 的开关 ✗。
```

## 三、当前状态（✓，可交接 ✓）

```
机制侧：三个缺口（ghost disarm ✓ / fast repair ✓ / requeue 前 settle ✓）**全部落地** ✓ 且各有真机读数 ✓；
实验侧：`setup_settle_us` 的 5000/50000 **不改变结论** ✗；
⇒ ⇒ **⇒ `main: victim uid=0` 仍未出现** ✗ ⇒ 目标**保持 active** ✓。
```

## 四、门禁（✓）

```
check → **109 PASS** ✓；lint → 0 ✓
```
