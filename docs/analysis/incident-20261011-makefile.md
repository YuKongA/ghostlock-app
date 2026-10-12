# 事故记录：core/Makefile 被整体写回成 HEAD（丢约 1650 行未提交内容）

## 事实

- 时间：2026-10-11 上午。起因：我为收口"按行删除"造成的多处损伤，选择了"从 HEAD 取原文 + 整体写回"。
- **判断失误**：HEAD 的 `core/Makefile` 只有 **1328 行 / 77 562 B**，而工作区那份有 **2978 行** ⇒ 工作区里有大量**未提交**成果。
- 结果：`make -C core check` 由 **114 PASS** 掉到 99 PASS；`android` 报 22 条错误。丢的内容包括 EVIDENCE_BINS 全表、
  `check` 配方里逐测试的调用行、各测试的构建规则、若干源列表条目。

## 没有丢的（关键）

- **所有源码与测试文件都在**（我只动了 Makefile；`build/native/ghostlock` 仍是最后一次成功构建的二进制）；
- 丢失内容的**权威副本仍在两处**：
  1. `/tmp/check_r248.log` —— 里面有**主机链接命令行**，逐项列出全部 host 源与库；
  2. 会话上下文里存有 **EVIDENCE_BINS 全表**（早前 grep 的整行输出）与 `check` 配方的形态。

## 恢复路径（下一步机械执行，可逐项核）

1. 用 `src/tests/*_test.cpp` 清单 + `check_r248.log` 的链接命令行，重建 EVIDENCE_BINS 与各测试规则；
2. 用日志里的 **host 源列表**重建 RUNTIME_SRCS；`ANDROID_ONLY_SRCS` 既有名单 + 本次四个新单元；
3. 复核：`check` 应回到 **≥113 PASS**（114 减去我删掉的重复品那一条），`android` 0 警；
4. 纪律补充：**Makefile 一律不许按行删或整体写回**；只做定点、可验证的插入，并**先备份到 `/tmp`**。

## 按规矩报告的字节/行数

- 写回前（HEAD）：1328 行 / 77 562 B；写回后：1328 行 / 77 738 B（仅多 4 个 ANDROID_ONLY_SRCS 条目）。
