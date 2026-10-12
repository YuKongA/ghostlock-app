# 新判据：`w2 => target-outside-direct-map`（2026-10-12 · 第 129 轮真机）

## 1. 判据变了（⇒ 本轮改动的直接后果 ✓）

```
此前：`main: step result w2 => **w2-not-rooted**` ✗
本轮：`main: step result w2 => **target-outside-direct-map**` ✓✓
⇒ 这不是失败换了个名字 ✗，而是**拒绝点前移**：从"提权没成"前移到"**目标地址不在直接映射区**"
  ⇒ ⇒ **⇒ 即：库**先**判地址区，再谈提权** ✓ ⇒ **⇒ 现在这条链的**第一个**卡点是地址，而不是提权** ✓✓
```

## 2. 机制（⇒ 与 `parity 0/24` 对上了 ✓）

```
日志逐字（真机 ✓，类型纪律生效后 ✓）：
  [warning] prepare parity base  **0xffff800000008000**   ← **直接映射区** ✓（⇒ 稳定 24/24 ✓）
  [warning] prepare parity delta **0xfffffffffffff180**   ← ＝ base − target ⇒ 负 ⇒ **目标低于 base** ✓
  [warning] prepare parity try a=N b=0                    ← `ok = 0` ⇒ **parity 全失败** ✓
⇒ ⇒ **⇒ 因果**：页在**直接映射区**（`0xffff80…` ✓）而**目标字段不在** ⇒ ⇒ **⇒ 偏移对不上**
   ⇒ ⇒ ⇒ **⇒ parity 必然为 0** ✓✓（⇒ 与判据 `target-outside-direct-map` **同一件事** ✓）
⇒ ⇒ ⇒ ⇒ **⇒ 故 `docs/analysis/43499-log-first-facts.md` §5 的"待查"**已有答案**** ✓：
     不是"负值"✗（⇒ 那是旧的数值重载印出来的 ✓），而是**页与目标不在同一个地址区** ✓✓
```

## 3. 下一步（⇒ 两条线，各自可失败 ✓）

```
① **核 `base` 的期望值**：与 **ref** 对照（⇒ `ghostlock-app-ref/src/core/memory/heap_context.cpp:13/:51` ✓）
   ⇒ ref 里 `page.base` 取什么 ✓ ⇒ 本树 `align_base(leaked, …)`（`prepare_page.hpp:507` ✓）是否同源 ✓
② **核"目标"的来源**：判据说目标**不在**直接映射区 ⇒ ⇒ **⇒ 目标该在哪** ✓ / **该不该被搬进该区** ✓
   判据（可失败 ✓）：真机出现 `prepare parity try a=N **b=1**` **任意一次** ✓（⇒ 已记在事实文档 §5 ✓）
   ＋ 真机 `w2` 不再出现 `target-outside-direct-map` ✓
```
