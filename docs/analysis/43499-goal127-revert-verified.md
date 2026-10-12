# 43499 · 第 127 轮：**第 124 轮回归已回退并验证** ✓✓

## 一、回退（✓，两处 ✓）

```
(A') 从 `waiter_main` 移除 `requeue();` ✓（⇒ 原位改为一行说明注释 ✓）；
(B') 把 `requeue();` **放回 `execute()`** ✓（⇒ 与第 124 轮之前一致 ✓）；
⇒ `grep -c 'requeue();'` = **1** ✓（⇒ 既无重复、也无遗漏 ✓）。
```

## 二、期间修掉的一处自身瑕疵（✓，必须记 ✓）

```
回退时我的替换**吞掉了下一段注释的开头** ✗ ⇒ `-Wcomment`（`'/*' within block comment`）✗；
⇒ 当场修正（把两段注释分开 ✓）⇒ 复核：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓。
```

## 三、验证（✓✓✓，判据是"整跑形态" ✓）

```
`main: step result **w1 => won**` ✓ ＋ `main: step result **w2 => w2-not-rooted**` ✓✓；
`step result` 行数 = **2** ✓（⇒ 回归前是 1 ✗）⇒ ⇒ **⇒ 形态已恢复** ✓；
`route status` × 14 ✓；`comm after len=15` × 14 ✗；`victim uid=2000` ✗（⇒ 基线不变 ✓）。
```

## 四、本轮的净结果（✓）

```
· 第 124 轮那两处改动**已完全回退** ✓ ⇒ 工作树回到"第 122 轮 + park 顺序修正"的形态 ✓；
· 第 126 轮发现的问题（`w2` 不执行 ✗）**已消除** ✓；
· 新增纪律已入账 ✓：**攻击路径改动后先核 `step result` 行数与步名** ✓（而不只看门禁与单条读数 ✓）。
⇒ ⇒ **⇒ `main: victim uid=0` 仍未出现** ✗ ⇒ 目标**保持 active** ✓。
```

## 五、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
