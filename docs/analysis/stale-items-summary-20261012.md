# 过期但未移除项 · 总表（2026-10-12 · 只读核实，未改任何文件）

## 0. 本件性质

本次「清理代码」审查期间，共核实出**三处**"旧的、过期的、但仍在树里"的对象。各自有独立明细件，
此处只做汇总与索引，便于一处看全。所有核实均为只读。

## 1. 三处总表

| # | 对象 | 实测 | 文档里写的 | 明细件 | 处置前置 |
|---|---|---|---|---|---|
| ① | 三件未跟踪产物与残留 | core/cmake-build-debug 876K · profile-core/bin 696K · core/.idea 108K | — | cleanup-candidates-20261012.md §1 | 需用户一句（非旧树产物，AGENTS §4 授权不精确覆盖） |
| ② | UML 的数字脱钩 | 暴露面 10 条 · manifest 29 行 | 7 条（六处）· 19 行（一处） | uml-stale-counts-20261012.md | :212 与 :215-232 需能构建（卡在 P4 的 mpack 引用面之后） |
| ③ | wire-schema 的一条待办自问自答 | 用户已裁定"载荷＝stdin / base64" | pending 第 ② 项仍写"载荷字节的通道（data[] 只是位置；字节沿用 stdin/base64）" | 本件 §2 | 直接可改（移出 pending），需用户一句 |

## 2. 第三处的依据（逐字）

```
用户裁定（docs/analysis/config-wire-redesign-plan.md:4）
  "③ 载荷＝stdin / base64；④ profile-core 删除，检查改由 Kotlin 承担；⑤ 运行期仍要有 Lua"

wire-schema.json 的 pending 两项
  ① "handoff.selection 是否随 wire 传入（供显示与追溯）"        <- 未在计划中见裁定，仍是真待办
  ② "载荷字节的通道（data[] 只是位置；字节沿用 stdin/base64）"  <- 括号里已给出答案，且与裁定 ③ 一致

=> 第 ② 项自问自答：它既问"通道是什么"，又在括号里写了"字节沿用 stdin/base64"。
   留着会让读者以为载荷通道未定 => 应从 pending 移出。
```

## 3. 与本次审查的删/留结论的关系

```
本表三处都不属于"删代码"：
  ① 是清产物与残留；② 是改文档数字；③ 是改契约件的待办列表。
本次审查对 core/src 的结论不变：留 50 件 / 删 4 件（其中 3 件即上表①，另 1 件是
pipeline/run_pipeline_temp，而它有已登记的删除条件与前置，见 cleanup-candidates-20261012.md §3）。
```

## 4. 已核并撤回的两处线索（2026-10-12，避免后人重查）

```
线索 A：UML 写"新树 9 层"，怀疑与实现脱钩
  实测 core/src/contract/layer_scope.hpp:33 的 kLayers[] 恰为 9 行
    contract(Op) · support(Op) · memory(Op) · backend(Op) · pipeline(Window) ·
    terminal(Op) · script(Script) · session(Op) · profile(Op)
  => 与 UML 一致 => 撤回，不属过期。

线索 B：docs/archive/ 有 38 件未列入索引
  实测索引头部逐字："本文件是 docs/archive/README.md 的子索引（≤8 KiB）：
                       恢复索引 + 门禁证据索引 + implementation 记录归档。总览/命名/规则见 README..."
  38 件中仅 1 件名含 gate（20261007-2237-43284-gate-m5-selection-plan.md，而它是 selection-plan
  不是门禁证据）=> 属范围分工，非缺漏 => 撤回，不属过期。
  附记：该子索引实测 7958 B / 8192（余 234 B），已逼近它自己写的 ≤8 KiB 上限。
```

## 5. 工作树未提交面（2026-10-12 核对，供判断批次边界）

```
git status --short 共 251 项：M 9 · D 117 · ?? 125
顶层分布：app 229 · docs 8 · core 5 · procedures 3 · .markdownlint-cli2.jsonc 1 · .vscode 1
         · AGENTS.md 1 · settings.gradle.kts 1 · profile 1 · tools 1

两点确认：
  ① 提交 e18d76af 时只纳入 core/ tools/ docs/ 是正确的：app/ 的 229 项（117 删除 + 125 新增）
     是一次大规模重构的中间态，不属于"日志系统 + 换库"那一批。并入会把他人在途重构卷进该批。
  ② 本次清理审查产出的六份文档全部是未跟踪新件，未混入任何他人改动；
     core/ 的 5 项里只有 log.hpp 属 native-core（本会话已多处标注）。
```
