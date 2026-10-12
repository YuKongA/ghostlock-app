# 43499 · 第 144 轮：瓶颈的**精确形状** —— 预建的**新鲜度** ✓✓

## 一、两边的循环对照（✓✓✓，逐字 ✓）

```cpp
// 退役 retry_write_stage（steps.cpp:57-110 ✓）：**循环体内**每次都做 ✓
for (attempt = 1; attempt <= attempts; attempt++) {
    if (attempt > 1 && verify(context)) { return true; }
    if (attempt == 1) { slab_drain(); }
    if (mode == Credential) { if (!M::**w2_fast_repair_prebuild**(session)) return false; }   // ★ 每轮预建 ✓
    routed = attack_write<M>(session, request, stage);
    if (mode == Credential) { if (!M::**w2_fast_repair_activate**(session)) return false; }   // ★ 每轮激活 ✓
    if (settle_usec) usleep(settle_usec);
    if (verify(context)) return true;
    usleep(50000);
}
⇒ ⇒ **⇒ 即："预建 → 写 → 激活"是**每一轮**都重做的循环体** ✓✓；
⇒ 而本树（第 103 轮起 ✓）：预建在**步首一次** ✗（⇒ `repair_prebuilt` 只置一次 ✓），
   修复轮 15 轮里**复用同一份预建页** ✗ ⇒ ⇒ **⇒ 这就是"预建新鲜度"差异** ✓。
```

## 二、为什么它可能是那一环（✓✓）

```
① `w2_fast_repair_activate` 的名字与位置说明：**激活 = 用刚预建好的页去跑一次修复路由** ✓；
② 而**每一轮**都要预建 ⇒ ⇒ **⇒ 暗示：一份预建页**只能用一次** ✗（⇒ 被 requeue 消费过即失效 ✓）；
③ 本树复用同一份 ⇒ ⇒ **⇒ 第 2~15 轮的"激活"用的是**已失效**的页** ✗✓ ⇒ 修复永不生效 ✓；
④ 这与"极性正确、形式全对、`uid` 仍不变"**完全一致** ✓✓（⇒ 第 1 轮或许也不成立 ✓）。
```

## 三、改动（✓，一处 ✓）

```
把"预建"从**步首一次**改为**每轮一次** ✓（⇒ 即把预建块移入重试循环内、`attack_write/修复路由之前` ✓）；
⇒ 本树已有 PageSlot（一次性 ✓）⇒ **语义天然吻合** ✓（⇒ 槽用一次即空 ⇒ 每轮自然要重预建 ✓）；
⇒ 判据：真机 `main: fast repair payload prebuilt` 出现 **15 次** ✓（⇒ 现为 1 次 ✗）⇒ ⇒ **再看 `uid`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
