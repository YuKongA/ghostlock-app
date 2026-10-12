# 43499 · `fake_fops` 线索细化：布局有 ✓、我的 select 配置没有 ✗（round 201）✓

## 本轮查清的三件事（都有行号 ✓）

```
1. 快照 `fake_fops` 来自**布局的 fops 槽**：:276 `parent/right/left/fops` ✓、:286 `layout.fops = default_fops` ✓、
   :295 Credential 模式换 `credential_fops` ✓ ⇒ 它不是"新数据" ✓ 而是**既有字段** ✓；
2. 本树**也有**这三个字段：write_layout.hpp:31 `fops` ✓、:43 `default_fops` ✓、:44 `credential_fops` ✓；
   main.cpp:645 读 `fops_table` ✓、:773 `layout_inputs.default_fops = payload_base + fops_table` ✓；
3. 我的 select 分支只喂 `fake_task` / `fake_lock` ✓（:930-931 ✓）⇒ **没有 fops** ✗（`SelectStackConfig` 里也没有该字段 ✗）。
```

## 这说明了什么（诚实 ✓）

- 页面**本身**由共用的 prepare/spray 造 ✓（日志里 `main: prepare status=ok` ✓）⇒ fops 槽**已经在了** ✓；
- 而快照的 `FakePage` 注释写着「真实值来自**堆喷**」✓ ⇒ 它的 `fake_lock`/`fake_fops` 是**泄漏来的值** ✓；
- ⇒ 因此问题不在于"页里有没有 fops" ✗，而在于**我的 route 是否把该值的来源接上** ✓；
- 下一批要读的是：**快照的写目标是什么**（写 fops 槽？写 task 槽？✓）⇒ 由它决定 select 是否真需要 fops ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
