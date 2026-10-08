# 附件：P2 评审归档的完整叙述与 Lead 复核更正

> 记录类附件（≤8192 B）。**主件** = [p2-review-20261008](p2-review-20261008.md)（含十行表原文）。本件承载主件压缩掉的叙述（评审要求「超配额则压缩叙述、保留表格与依据」）与 **Lead 复核更正**。

## §A1 验证了什么 / 没验证什么（原文口径）

- **验证了**：97173d9f 的文件清单与计数（61 个 `.h/.hpp` = 58 核心头 + 3 host shim；新增 `.cpp` = **0**）· annex-a1 48 行与交付 58 头的集合差集（10 头逐一列出）· `.disabled` 实测 **197** 与 manifest **198 行**逐条对账（唯一不对应项 = `src/Makefile` guard 行）· 3 对 shim 的 sha256 逐字节比对与 `97173d9f^` 尺寸 · 兼容头 vs 旧头尺寸与关键符号活定义点（`TargetProfile`/`kernel_offsets`/`kBackendStateBytes`）· 冻结断言 4 处 `file:line` · compat-check 提交态源码（含末行断言）· spec §6 判据与 gate doc §2/§4 证据范围 · 5 份文档字节数 · git 状态（backend 0 改动；`src/Makefile` 为 M）。
- **没验证**：**未跑任何门禁**（只读约束）—— compat-check 97/0 · 宿主 87/10 · C ABI `-fsyntax-only` 退出 0 · 证伪实验（1024 双错误 + FALSIFY-EXIT=1）· `sizeof(Cve2026_43499State)=1560` 均为**引述 gate doc**，未独立复跑；未核 59 依赖子集与 97 全集的 include 解析脚本；未核 P4 新文档与未提交 Makefile（不覆盖）；未核 `.disabled` 正文与旧提交的逐字节一致性（只抽验 3 对 shim）。

## §A2 可检索记录

2026-10-08 17:54 · 原则版本 v0.2 · 总判「仍需修订」· 阻断数 0（重要 6 / 建议 4）· 快照 HEAD c100112d · 被审提交 97173d9f

## §A3 Lead 复核更正（2026-10-08 · **取代**评审「已核事实 ①」）

- **更正**：评审「已核事实 ①」称新 compat 头为**最小化实现**（并给 `identity.hpp` 603 B）—— **与物证不符**。
- **Lead 实测（全量脚本 + 直接 sha256 抽查）**：**61 个活头** = **56 个与 `.disabled` 孪生件逐字节相同**（含 `identity.hpp` **28398 B**、`capabilities.hpp` **2619 B**、`model.hpp` **19235 B**，sha256 逐对相等）**+ 2 个受控差异**（`support/log.hpp` **8448 vs 8054**，增 `__ARM` 沿革注释；`session/core_session.hpp` **2344 vs 2006**，增冻结断言）**+ 3 个新件**（P4：`pipeline/attack_window.hpp`、`pipeline/composition_root.hpp`、`contract/resource_scope.hpp`）。
- **归档者同口径复核**：活头 vs 同名 `.disabled` 的 sha256 比对 ⇒ **live 64 = 59 逐字节相同 + 2 有差异 + 3 新件**；由 `64 − 3 host shim = 61`、`59 − 3 = 56` ⇒ **与 Lead 数字对账一致**。
- **复核方法**：遍历 `src/core/**` 中非 `.disabled` 的 `.h/.hpp`，与路径同名 + `.disabled` 的孪生件比对 sha256（脚本同 [A3 §A4](contract-layer-p2-spec-annex-a3.md) 的 include 解析口径）。
- **影响**：本更正**取代**评审的最小化结论；F5 的偏差登记同步改写为「**整层保形逐字复制**」（见 [gate doc](p2-compat-gate-20261008.md) §7 **U-4**）。
