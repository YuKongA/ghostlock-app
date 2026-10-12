# 43499 · 我缺的那一组：spray 之后、snitch 之前的 31 子进程（round 141）

## 退役的五个 ctx（spray.cpp:272-281 逐字 ✓）

```cpp
prepare_ctx.childs = 8 x mm_objs_per_slab        = 256   ← spray **之前**
spray_ctx.childs   = (1 + MM_PARTIALS) x ……      = 192
pre_ctx.childs     = mm_objs_per_slab - 1        = **31**  ← spray **之后**、snitch 之前
post_ctx.childs    = mm_objs_per_slab            = 32
```

## 本树（prepare_page.hpp:250-333 ✓）

```
for i < plan.pre_groups     （spray 之前 ✓）      ← 对应退役的 **prepare** ✓
for i < plan.spray_groups   （spray ✓）
snitch.start(...)
for i < plan.post_groups    （post ✓）
leak_child (单个 ✓)
⇒ ⇒ **没有**"spray 之后、snitch 之前"那一组 ✗
```

## 为什么它重要

- 它恰好夹在 **spray 与泄漏** 之间 ⇒ 作用是"把泄漏目标的落点整形" ✓；
- 而奇偶正是"泄漏到哪个 mm_struct 的 bit16" ✓ ⇒ ⇒ 它极可能就是奇偶抖动的**直接来源** ✓；
- 实测：奇偶完整率约 1/3 跑（round 141：2/3 被 `payload-layout-not-accepted` 拦 ✗）。

## 下一步（代码改动，规格 ✓）

1. 在 `prepare_page` 的 spray 之后、`snitch.start` 之前，插入一组子进程（数量走**文档** ✓）；
2. 文档键：新增 `backend.cve_2026_43499.execution.heap.pre_shaped_groups`（值 31 = `mm_objs_per_slab − 1` ✓），
   或复用既有字段名并按退役语义解释（一处权威 ✓，择一）；
3. 判据：连续 3 次跑均无 `payload-layout-not-accepted` ✓（三步 `accepts=1` ✓）；随后回到 `uid=0` ✓。
