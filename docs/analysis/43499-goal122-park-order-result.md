# 43499 · 第 122 轮：park 顺序修正（有据、门禁绿、效果未现）✓✗

## 一、改了什么（✓，一处移动 ✓）

```cpp
// waiter_main：`waiter_waiting` 由"park **之前**"✗ 改为"park **返回之后**"✓
const std::int64_t park_us = …;                                   // 原 :473 的置位行移走 ✓
parked = Ops::wait_requeue_pi_until(&chain_word, &target_word, park_us);
shared_.waiter_waiting.store(1);   /* ★ park **已完成** ⇒ 此时才放行 requeue ✓ */
⇒ 依据：`requeue()`（`:342` ✓）等的就是这个标志 ✓ ⇒ 原先"一置位即可发起 requeue"✗ ⇒
  内核可能在 waiter 尚未 park 时搬链 ⇒ 拷贝无处落 ✗ ⇒ 现与 park 串行 ✓（与退役同族 ✓）。
```

## 二、结果（✓✗，如实 ✓）

```
门禁：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓；
真机：`main: victim comm after len=15 first=103` × **13** ✗（⇒ 独立旁证仍说"写没落" ✓）；
      `main: victim uid=2000` ✗；`main: route status=0/0 clean=1/1 attempts=16` ✓。
⇒ ⇒ **⇒ 修正有据、构建干净、而效果未出现** ✗。
```

## 三、当前的准确认识（✓✓，可交接 ✓）

```
① 输入侧**全部正确** ✓（页内容逐字相等 ✓ / 目标同族 ✓ / 写值 = `init_cred` 别名 ✓ / 编码 ✓ / 窗口 14/14 clean ✓）；
② 结构侧的几处差异**已逐条补齐或更正** ✓（ghost disarm ✓ / fast repair 预建+激活 ✓ / settle 本就存在 ✓ /
   park-requeue 顺序 ✓）；
③ 而**写仍未落** ✗（`comm len=15` 恒定 ✓）⇒ ⇒ **⇒ 剩余候选**：
   · `requeue` 的**宿主线程**（本树：调用线程 ✓；退役：race 线程 ✓）⇒ 尚未对齐 ✗；
   · 或**页的生命周期**（本树在 waiter 线程内 `trigger` 后仍持有 ✓；退役由 race 线程送 ✓）✗；
   · 或内核侧对该目标的**语义校验**（`_ref`/大小 ✓）✗。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
