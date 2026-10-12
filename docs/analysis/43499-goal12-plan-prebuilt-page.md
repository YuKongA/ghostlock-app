# 43499 · 实现计划（L 级）：W2 的「预建页」机制（新目标第 12 轮）✓✓

> **进度（第 13 轮 ✓）**：切片 1 已完成 —— `core/src/backend/cve_2026_43499/memory/page_slot.hpp` ✓
> （一次性槽：`stash` / `activate` / `discard` ✓；不拥有字节 ✓、零分配 ✓、无全局 ✓、
> 出口全覆盖的要求写在文件头 ✓）；`check` 108 PASS ✓ / `android` 0 警 ✓。
> **切片 1 已自证（第 14 轮 ✓）**：新增 `core/src/tests/page_slot_test.cpp` ✓ 并按本树的三处约定注册
> （`EVIDENCE_BINS` ✓ + 构建规则 ✓ + `check:` 运行行 ✓ —— 只改 `EVIDENCE_TESTS` 是**不够**的 ✗，
> 那是"标准检查"用的清单 ✓）；`check` 由 108 → **109 PASS** ✓，日志出现
> `check: page slot (the one-shot W2 repair slot)` ✓ ⇒ 拒绝语义（二次 stash / 二次 activate / 空页 ✓）已被主机门禁守住 ✓。
>
> 切片 2（W2 分支的 prebuild→主写→activate 接线 ✓）与切片 3（现修复轮降为回退 ✓）待做 ✓；
> 待办：按守则 §8 做一次"造错 ⇒ 预期失败 ⇒ 撤回"的证伪（本轮未做 ✗）。

## 一、为什么（✓，证据链 ✓）

```
退役 W2 的时序（`multicast_waiter_route.cpp:170/187` + `retry_write_stage` ✓）：
   ① `w2_fast_repair_prebuild()`：用**修复请求**（`data_alias(init_cred)+8`, Zero, leaf=1 ✓）
      调 `spray::prepare_good_kernel_page(repair_request)` ✓ ⇒ `spray::stash_prebuilt_page()` ✓（**存起来** ✓）；
   ② `attack_write`（主 cred 写 ✓）；
   ③ `w2_fast_repair_activate()`：`activate_prebuilt_page()` ✓ ⇒ 用**存好的页**跑**修复路由** ✓；
   ④ settle + verify（受害者 uid ✓）。
本树现状：这套机制**一个都没有** ✗（`prepare_good_kernel_page` / `stash_` / `activate_` / `discard_` 全 0 ✓，
  退役分别 6/3/3/5 个文件 ✓）；本树的做法是"主写后**整重跑一遍 prepare** 再发页再跑一轮" ✗ ⇒
  ⇒ ① 代价大（第 5/7/8 轮的 fd/pid 泄漏皆源于此 ✓）；② **不是"预建"** ✗ ⇒ 与退役不同形 ✓。
```

## 二、要做什么（✓，按退役逐条对齐 ✓）

| # | 事项 | 落点 |
| --- | --- | --- |
| 1 | 新增**一次性页槽**：`stash` / `activate` / `discard` 三个动作（页内容已由既有 `build_payload_page` 产出 ✓） | `backend/cve_2026_43499/memory/` 新文件 ✓ |
| 2 | W2 分支：**路由前** `prebuild`（用修复请求 ✓）⇒ 主写 ⇒ `activate`（跑修复路由 ✓） | `main.cpp` 步骤体 ✓ |
| 3 | 任一路径（成功/失败/重试/取消）**必丢弃**槽 ✓（守则 §5：终结点全覆盖 ✓） | 同上 ✓ |
| 4 | 保留现有"修复轮"作为**回退** ✓，由同一开关选择 ⇒ 便于 A/B ✓ | 同上 ✓ |

```
开关与数字：一律取自 wire/profile ✓（不写死 ✗）；新槽不得引入可变全局（守则 §4 ✓）。
```

## 三、判据（✓，先定后做 ✓）

```
主判据：任一轮 `main: victim uid=0` ✓（唯一硬判据 ✓）；
辅助：`main: fast-repair stashed/activated/discarded` 三类事件按预期出现 ✓；
回归：`route status=0/0 attempts=16 calls=1 successes=1` 与现值一致 ✓；无 EAGAIN ✓；无僵尸 ✓；
门禁：`make -C core check` 108 PASS ✓ + `make -C core android` 0 警 ✓ + `./gradlew markdownlint` 0 ✓。
```

## 四、风险与回退（✓）

```
风险：槽的生命周期若漏丢弃 ⇒ 回到第 5/7 轮的泄漏 ✗ ⇒ 故第 3 条是**硬要求** ✓；
回退：机制独立 ✓ ⇒ 关掉开关即回到"整重跑修复轮" ✓（一行 ✓）；旧码按守则 §3 套住并标 TODO ✓。
```
