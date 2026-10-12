# 43499 · 第 96 轮：预建/激活的**粒度**澄清（每次尝试前，不是每步一次）✓

## 一、退役的粒度（✓✓，`retry_write_stage` 逐字 ✓）

```cpp
for (attempt = 1; attempt <= attempts; attempt++) {
    if (attempt > 1 && verify(context)) { return true; }
    if (attempt == 1) { slab_drain(); }
    if (mode == Credential) { if (!M::w2_fast_repair_prebuild(session)) { return false; } }   // ★ **每次尝试前** ✓
    Status routed = attack_write<M>(session, request, stage);                                 // 主写 ✓
    if (mode == Credential) { if (!M::w2_fast_repair_activate(session)) { return false; } }   // ★ 之后激活 ✓
    if (settle_usec) { usleep(settle_usec); }
    if (verify(context)) { return true; }
}
```

## 二、澄清（✓✓）

```
⇒ ⇒ **⇒ 预建/激活的粒度 = 每次尝试** ✗（⇒ 不是"每步一次" ✓）；
本树：**每步**一个"修复轮" ✗ ⇒ 但在**步骤级重试**下（第 6 轮 ✓，每轮整跑一步 ✓）≈ **每轮一次** ✓；
⇒ ⇒ 因此粒度**大体等价** ✓（⇒ 不是主要差异 ✗）⇒ ⇒ **⇒ 主要差异仍是"位置"**（退役在**主写之前** ✓，本树在**之后** ✗）✓。
```

## 三、对清单的影响（✓，不改变结论 ✓）

```
搬移清单（第 94 轮 ✓）**不变** ✓：把「prepare + 建页 + 入槽」提到主路由之前 ✓；
⇒ 本轮只是把"**为什么必须提前**"补全 ✓：退役是"**先备好、再写、再激活**" ✓，
   本树是"先写、再备好、再激活" ✗ ⇒ ⇒ 即**顺序本身**可能就是机制的一部分 ✓（⇒ 与本会话的机制层结论一致 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
