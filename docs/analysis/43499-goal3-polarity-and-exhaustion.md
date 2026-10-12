# 43499 · 新目标第 3 轮：极性判定、W2b 臂修正、耗尽点定位 ✓✓

## 一、极性判定（✓，靠**规则句**而非注释 ✓）

```
write_layout.hpp:12-16「a write that **preserves the child** must **carry a value**,
                      and one that does not must carry none」✓
退役 `WriteRequest::make(target, mode, leaf != 0)` ⇒ `preserve_child = (leaf != 0)` ✓
⇒ 三步主请求（leaf：W1=0 / W2=0 / W3=1 ✓）⇒ preserve = false / false / true ✓ —— **与第 2 轮所改一致** ✓；
⇒ 归档元组（pc→value / right→0 / left→target ✓）正是 **preserve 臂**（带 value ✓）⇒ 规则句互相印证 ✓。
```

## 二、本轮修的第三处臂反（✓）

```
两处**修复请求**（W2b ✓，诊断处 + 修复轮 ✓）：退役 `WriteRequest::make(data_alias(init_cred)+8, Zero, **1**)`
⇒ preserve=**true** ✗（旧 false ✗）⇒ 已改 ✓（`replace_all` ✓，两处同改 ✓）；
⇒ `android` → RC=0 / 0 警 ✓；`check` → 108 PASS ✓。
```

## 三、新的主导阻塞（✓，本轮定位 ✓）

```
真机三跑均：`w1 => won` ✓ ⇒ `w2` round=1..7 各自 `victim spawned` + `uid=2000` ✗ ⇒
  第 8 轮：`main: discovery verdict=**call-failed** probes=0 rounds=0 found=0x0` ✗
  ⇒ `main: refused (victim-fork-failed)` ✗ ⇒ 整步步失败 ✓。
⇒ ⇒ **不是进程耗尽** ✗ 而是 **discovery（泄漏搜索）的调用在约第 8 次失败** ✗；
   `prepare region=8328 bytes` ✓ 仍正常 ⇒ 准备本身未崩 ✓。
⇒ ⇒ 成因：我实现的"步骤级重试"**每轮整跑一遍 prepare/discovery** ✗（代价 ~7 轮即尽 ✓），
   而退役的 `retry_write_stage` 是**在已准备状态内**重试 route ✓（准备只做一次 ✓）。
```

## 四、下一步（✓，二选一即可推进）

| # | 修法 | 代价 |
| --- | --- | --- |
| A | **把重试下沉**：准备/发现只做一次，重试只重跑 route ✓ | 中等改动（步骤体分段 ✓） |
| B | 保留外层重试，但**每轮之间清理** discovery 的持有物（rings/mmap/fd ✓） | 需先定位持有物 ✓ |

```
倾向 A（✓）：它与退役同形 ✓，且顺带把 15 轮的代价降到可行 ✓；
随后才能判"臂/修复修正"是否真的让 w2 落地 ✓。
```
