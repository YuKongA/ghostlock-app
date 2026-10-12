# 43499 · 新目标第 4 轮：fd 泄漏确证（重试触顶的真因）✓✓

## 一、确证（✓，一次跑内的递增序列 ✓）

```
`PFOPEN`（受害者的 perf open ✓）报告的 fd 号：
  512 → 5077 → 6083 → 7094 → 11668 → … → 23820 → 24813          ✗ 单调递增
⇒ ⇒ **每次 prepare 泄漏 ~1–5k 个 fd** ✓；设备 `Max open files = 32768` ✓（hard 亦 32768 ⇒ 不能再提 ✗）；
⇒ ⇒ 于是约 7–8 轮即触顶 ⇒ `discovery verdict=call-failed probes=0` ✗ ⇒ `victim-fork-failed` ✗ ✓。
```

## 二、树里其实**已有**回收机制（✓，说明是"没接上"而非"没写" ✓）

```
prepare_page.hpp:166  /** Give every descriptor back, in the order the reclaim plan names. */ ✓
prepare_page.hpp:208  case ReclaimGroup::Pre/Spray/Post ⇒ ++(*closed_pre)/… ✓（按**个数**计数 ✓）
pipeline/attack_window.hpp:40  /** RAII reclaim scope: **the one guard every exit path must run through** ✓
  同文件 10-11：reclaim on EVERY exit path (success/failure/retry/cancel/early return)
                —— "the historical PI panic came from a missed reclaim" ✓（教训已写进注释 ✓）
⇒ 但真机读数 `prepare groups … **close=0/0/0**` ✗ 与 fd 号的单调递增 ✗ ⇒ **设备路径上回收未生效** ✓。
```

## 三、下一步（✓，二选一）

| # | 修法 | 说明 |
| --- | --- | --- |
| A | 让设备路径**走 `attack_window` 的 RAII 回收** ✓ | 设计已存在（"every exit path" ✓）⇒ 优先 ✓ |
| B | 在每步/每轮结束时显式调用 prepare 的 reclaim ✓（`prepare_page.hpp:166` ✓） | 最小改动 ✓ |

```
验收判据：`PFOPEN fd=` **不再递增**（稳定在数百 ✓）且 `close=…` 非零 ✓ ⇒ 之后 15 轮才跑得动 ✓，
才能判定第 2/3 轮修正的"三步臂 + W2b 修复"是否真让 `w2` 落地 ✓。
```

## 四、本轮其它事实（✓）

```
· 三步臂的极性已由**规则句**判定（`write_layout.hpp:12` ✓）⇒ 主请求 false/false/true ✓、修复请求 true ✓；
· 构建与门禁保持全绿：android RC=0 / 0 警 ✓；check 108 PASS ✓；lint 0 ✓；
· 真机仍未 root ✗（`victim uid=2000` ✓）——因为重试在触顶前只跑了 7 轮 ✓。
```
