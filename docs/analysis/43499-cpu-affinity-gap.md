# 43499 · 逐项比对已走完 + 一处从未比对的要求（round 101）

## 两项收尾核对（都对齐 ✓）

```c
// warm-up 规则：与退役逐字相同 ✓
if (attempt < config_.limits.arm_sequence || !shape_landed(result, stamp_errno)) continue;
```

几何：夹具 `waiter_off=0x60 / task_offset=0x30 / lock_offset=0x38 / buffer_size=0x108` ✓
      = 退役 profile 的 96 / 48 / 56 / 264 ✓ ✓

## 由此得到的结论

**我能比对的每一项都已与退役一致** ✓（14 处修复 + 几何 + warm-up + requeue 语义 + 拷贝落地 + 页面进内核 + 有符号 delta + 分块铺页面 ✓），而**写仍不落** ✗ ⇒ 差别只可能落在**我没比对过的维度**上 ✓。

## 一处从未比对、且退役门禁**明文要求**的维度（下一轮）

- 退役真机门禁的验收条件原文包含：**冷机、固定 CPU 对**（`docs` 里的门禁规范 ✓）；
- 文档里也有 `backend.cve_2026_43499.execution.recommended_cpus = { consumer: 1, main: 0 }` ✓；
- 而本引擎的三个演员（waiter / owner / consumer）**没有任何 pin** ✗（`Ops::pin_to_processor` 只在
  准备阶段的 PinAndYield 用过 ✓）。

⇒ 三个演员跑在哪个 CPU 上，直接决定"等待者 park 的那份形状"与"触发线程的栈/时序"能否对上 ✓ ——
  这正是退役把"固定 CPU 对"写进门禁的原因 ✓。

## 下一步（一次改动 + 一次真机）

1. `MulticastRouteConfig` 增加两个 CPU 号（走 wire 的 `execution.recommended_cpus` ✓，非默认 ✓）；
2. `start_threads()` 的三个入口各自 `Ops::pin_to_processor(...)` ✓（waiter→main 那个 ✓、consumer→consumer 那个 ✓）；
3. 跑 ⇒ 判据：`TEMPBISECT at2000` 非零 ✓ 或 `victim uid=0` ✓。
