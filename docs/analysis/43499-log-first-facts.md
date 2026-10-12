# 日志首次揭示的两条事实（2026-10-12，真机 3395 行 · 全量输出所得）

## 0. 来源（纪律 ✓）

> 「先输出完整的日志，然后再用grep截取」⇒ 本轮**先全量** ⇒ 前 12 行 ＋ 末 6 行**直接**暴露以下两条 ✓

## 1. `prepare parity` **40/40 全部失败**（`ok=0`）✗

```
日志逐字（真机 ✓）：
  [warning] prepare parity try a=1 b=0
  [warning] prepare parity base a=-140737488224256 b=-3712
  … 连续到 try=40 ⇒ 计数 `prepare parity try` = **40** ✓（⇒ 全失败 ✓）
⇒ ⇒ **⇒ 两个值都异常**：`b=0` 是 `parity_ok` ✗ ⇒ **从未成功** ✓；
   `a=-140737488224256`（= `payload_delta` ✗ 负值 ✓）／`b=-3712`（= `prepared.base` ✗ **负地址** ✓）
⇒ ⇒ ⇒ **⇒ 事实**：我们一直以为 prepare 在正常轮转 ✗ —— 日志显示它**从第一步就是负值** ✓✓
```

### 待查（下一步 ✓）

```
· `prepared.base` 为负 ⇒ 页面基址**未取得**或**取错** ✓ ⇒ 查 `prepare_runner` 的 base 来源 ✓
· `payload_delta` 负 ⇒ 与 base 同源 ✓ ⇒ 同上 ✓
· 参照：ref 的 prepare（⇒ `ghostlock-app-ref/src/core/` 的对应段 ✓）⇒ 两边 base 的**取得方式**是否不同 ✓
· 判据（可失败 ✓）：修复后真机出现 `prepare parity try a=N b=1`（⇒ `ok=1` ✓）任意一次 ✓
```

## 2. `retry -> uid not zero` 的 `a = 4294967295`（sentinel）✓

```
日志逐字（真机 ✓）：
  [debug]  victim -> uid value=2000            ← 读到**真实 uid**，但**未提权** ✗
  [warning] retry -> uid not zero a=4294967295 b=15   ← ★ `a = 0xffffffff` ＝ **sentinel** ✓
⇒ ⇒ **⇒ 两种失败形态**并存且**现在可辨** ✓✓：
   · `value=2000` ⇒ uid 通道**读成功**、但**没提权** ✗
   · `a=4294967295` ⇒ uid 通道**读失败**（⇒ sentinel ✓，由 `main.cpp:117` 的定义 ✓）
⇒ ⇒ ⇒ 判据：真机出现 `[warning] victim -> uid value=0`（⇒ 0 ⇒ **warn** 级 ✓）⇒ 目标达成 ✓✓
```

## 3. 其他读数（本轮 ✓，一并留档）

```
  park-about a=0 b=12160        ⇒ `wait_word = 0` ✓ ／ `target_word` 地址 ✓（⇒ 与 12160 的偏移 ✓）
  park-return a=-1 b=110        ⇒ ETIMEDOUT ✗（⇒ 与本会话此前一致 ✓）
  ghost-disarm a=-1 b=110       ⇒ ETIMEDOUT ✗
  （**读数出处更新（2026-10-12）**：环已撤除 ⇒ **原 `window-trace-counters` 一行消失**（它是环的元数据）；现存读数改为 `shared_` 字段 + **PI 之后**打印的 6 类行；实测 **a=5 b=0**、256 槽远未溢出 ⇒ **无信息损失** ✓）
  judge-outcome ×496 ✓ ／ attempt-door ×528 ✓ ／ window-trace-counters ×32 ✓
  日志规模 **3395 行** ✓（⇒ 704 → 2240 → 2801 → **3395** ✓）
```
