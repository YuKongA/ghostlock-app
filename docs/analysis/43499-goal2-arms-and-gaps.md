# 43499 · 新目标第 2 轮：重试已实现、臂已修正、缺口已定位 ✓✓

## 一、本轮的落地（✓，构建与门禁全绿 ✓）

```
① **步骤级重试**（main.cpp:1391 ✓，与退役 `retry_write_stage` 同义 ✓）：
   轮数/间隔取自 wire（`execution.stages.<step>_attempts`/`_settle_us` ✓），判据 w2 ⇒ **受害者自报 uid==0** ✓；
   真机验证：`main: step retry w1 round=1..4 of=15` ✓ ⇒ 第 5 轮过 ⇒ `step result w1 => won` ✓。
② **三步的 preserve 臂全被写反** ✗ ⇒ 已按退役**公式**修正 ✓（`WriteRequest::make(target, mode, leaf != 0)` ✓）：
   W1 `steps.cpp:342` leaf=0 ⇒ preserve=**false** ✓（旧 true ✗）；
   W2 `steps.cpp:164` leaf=0 ⇒ preserve=**false** ✓（旧 true ✗）；
   W3 `steps.cpp:214` leaf=1 ⇒ preserve=**true**  ✓（旧 false ✗）；
   （错源：文件里那段注释把 `leaf=0（preserve ✓）` 写反了 ✗ ⇒ 代码跟着错 ✓；已连注释一并订正 ✓。）
③ 测试随语义更新（`route_config_test.cpp`：零值合法 ✓）⇒ `check` 108 PASS ✓ / android 0 警 ✓ / lint 0 ✓。
```

## 二、学到的一条硬事实（✓）

```
夹具的 Lua **硬断言**文档里的 stages 值 ✓：`assert(w1 == 15 and w1s == 100000)` / `assert(w2 == 15)` /
`assert(w3r == 3)` ✓ ⇒ 我把 w1/w2 改成 8/5 ⇒ 脚本断言失败 ⇒ `main: refused (**plan-syntax**)` ✓
⇒ ⇒ 因此**不能随意改夹具的 stages 数字** ✗（它们既是数据、也被脚本当契约核对 ✓）。
```

## 三、下一步的缺口（✓，逐条都有退役出处 ✓）

| 缺件 | 本树 | 退役 | 作用 |
| --- | --- | --- | --- |
| `slab_drain` | 0 ✗ | 4 ✓ | 每阶段**首次**先排空 slab ✓ |
| `w2_fast_repair_prebuild` / `activate` | 1 ? | 5 ✓ | **Credential 专用**：路由前预建修复页、路由后激活 ✓ |
| `in_direct_map(target)` | 0 ✗ | 5 ✓ | 目标不在直映射区 ⇒ 不尝试（省下无效轮 ✓） |
| `discard_prebuilt_page` | 0 ✗ | 5 ✓ | 路由失败 ⇒ 丢弃预建页 ✓ |

## 四、真机现状（✓，臂修正后）

```
`w1 => won` ✓；`w2` 重试 round=1..5，每轮 `victim uid=2000` ✗ ⇒ **仍未 root** ✓；
⇒ ⇒ 结论：臂修正是**必要但不充分** ✓ ⇒ 下一步补第三节的 Credential 专用件（`w2_fast_repair*` ✓）。
```
