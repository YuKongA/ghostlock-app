# 事故记录（续）：core/Makefile 恢复进度与下一步

## 当前状态（2026-10-11 中午，如实）

- `make -C core -j1 check` → **107 PASS**（事故刚发生时是 99 PASS，原始是 114 PASS）
  - ✅ 已恢复：`RUNTIME_SRCS`（按记录的链接命令行重建 ✓）、`EVIDENCE_BINS +=` 六个补充项 + 我新加的两个测试、
    `check` 配方里各测试的调用行、补充测试的统一规则（用 `TEST_SRCS` ✓）、lua 头路径 `-I$(LUA_DIR)` 与 `$(LUA_CORE_LIB)`；
  - ❌ 唯一仍红：**`plan-check`**（输出 `main: wire refused (json-bad-number)` ⇒ 判据要 `queue-removed` 未出现）；
- `make -C core -B android` → **22 条错误**（`DEVICE_EXTRA_SRCS` 只加给了主机骨架那一处，设备配方未同步 ⇒ 缺 11 个单元）；
- 源码与测试**全部完好** ✓（我只改过 Makefile）；`build/native/ghostlock` 仍是最后一次成功构建的二进制 ✓。

## 为什么还没好（已知的坑）

1. HEAD 的 `RUNTIME_SRCS` 只有 21 个文件，**链不上今天的主程序**（主程序引用会话新增单元）⇒ 必须用"记录的链接命令行"那一份 ✓；
2. 但用那一份时 `plan-check` 变红：它的配方与 HEAD **逐字相同**（已核对 ✓）、夹具**未被改动**（已核对 ✓）
   ⇒ 差别只可能在**骨架的源集合/编译参数**上 ⇒ 需要**逐项二分**（11 个多出来的文件，一个一个去掉再跑，每次约一分钟）；
3. `android` 的配方还要把 `DEVICE_EXTRA_SRCS` 补进去（一处漏改）。

## 下一步（机械、可核、有备份）

1. 先在 `android` 配方补 `$(DEVICE_EXTRA_SRCS)` ⇒ 复核 `android` 回到 0 警；
2. 对 `plan-check` 做**二分**：在骨架源集合里逐个去掉那 11 个文件（每步 `make -B` + `plan-check`），
   找出让 `queue-removed` 重新出现的那一个 ⇒ 定点修正；
3. 全过程**每步先备份到 `/tmp/Makefile.stepN.bak`**（已有 N=1..8 ✓）；
4. 纪律（已写进 §恢复路径 第 4 条）：**Makefile 不许按行删、不许整体写回**，只做定点插入并先备份。

## 教训（一句话）

在"改坏了一点"的时候，我用一个**覆盖范围大得多**的动作去修，结果把小损伤放大成了 1650 行的丢失 —— 正确做法是**先备份 + 只做最小定点修复**。
