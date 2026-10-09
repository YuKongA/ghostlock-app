# 沿革：script-carriage-design v2 的逐条处置（**从主件迁出**，2026-10-08）

> 分析类（≤8192 B）。**迁出理由** = 配额：主件 `script-carriage-design.md` 正文按评审 R1–R6 落条文后达 9243 B > 8192 ⇒ 按配额纪律把「**v2 对二审的逐条处置表**」移到本件（**内容未改**，仅位置迁移）。
> 主件： [script-carriage-design.md](script-carriage-design.md)（正文保留结论）· 相关：[op-fall-rule.md](op-fall-rule.md) · [script-plan-design.md](script-plan-design.md)。

## 0 v2 改了什么（对二审的逐条处置）

| 编号 | 处置 |
|---|---|
| **CAR-1（阻断）** | **改走既有 payload 家族** ✓：不再新增根级 `str` 载具 ✗。理由：树里 `backend.<backend>.payload.<name>.{bin,sha256,size,kind}` 已实现并被 `Document::payload()` 读取（三键齐备 + `bin` 长度 == `size` 已校验 ✓），且 **kind 词表的 `plan` 语义正是「the plan」** ✓（`payload_keys.hpp:27-28` 注释原文：a plan / a finishing script / a module）⇒ `plan` 就是本裁定第 3 条的「计划 = 脚本」✓ |
| **CAR-2（重要）** | **随 CAR-1 解决** ✓：`PayloadEntry{.bytes,.size,…}` 的长度是显式的 ✓（`glkv3_document.hpp:46-57` 明写「length is explicit」）⇒ 「不经 C 字符串」由家族保证 ✓；`root_text` 无长度出参的问题不再相关 ✓ |
| **CAR-3（重要）** | 上限**复用既有常量** ✓ `script/sandbox.hpp:39` 的 `kScriptSizeCeiling = 64 KiB`（A6 基线 item 2 ✓），**不自造第二个数** ✗；若将来因分层必须两份 ⇒ 加 `static_assert` 相等并写明理由 ✓。上限**不塞进 manifest 列** ✗（列序无字节上限列 ✓）；它由**校验代码的常量**承载 ✓ |
| **CAR-4（重要）** | 行数按**当前实测**给：双副本 manifest 现为 **121 行** ✓（`62ea112b2c` ✓）；B1 重冻的算式以 `make -C src profile-manifest-v3` 的**实出**为准 ✓，不在设计里写死数字 ✗（v1 的「110 → 109」是陈旧引用 ✗） |
| **CAR-5（重要）** | 影响面补 **step_set/StepId 连带退役清单** ✓（见 §4.3） |
| **CAR-6（建议）** | ④ 反转并给判据：**摘要随家族必填** ✓（三绑定键之一 ✓）。v1 的「不加摘要」作废 ✗ |
| **CAR-7（建议）** | 判据补**边界值** ✓（见 §4.6：恰好 64 KiB / +1 / 空脚本 ✓） |
