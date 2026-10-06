# Terminal 接管后的「payload 三档」设计（L 级；已按 Lead 裁决修订 r2，待用户确认后实现）

> 状态：**设计终稿（r2），不含实现**。需求：用户可三档自定义——① 以 root 执行指定命令/可执行程序；② 以 LKM 执行指定脚本；③ 加载一个或多个 `.ko`。
> 依据：AGENTS.md、`docs/analysis/contract-design.md`、ADR-0006、`docs/development/design-philosophy.md`。
> r2 变更：字段用语义词、多 ko 键序、单档互斥、**统一失败语义**、可用性矩阵、授权面与执行前摘要、argv 与脚本区别写清。

## 1. 定位：payload 是「接管之后做什么」，不是新 terminal token

- **不新增** combination token、不动 `kCombinationCatalog`（避免组合目录按档数炸开，与 R6b 冲突）；
- payload 是执行策略，进文档（GLKv3 wire），落 **新顶层 owner 段 `payload`**（与 `plugin` 同级）；
- 与 terminal 正交：terminal 决定「如何接管」（`root_child` / `umh_forward`），payload 决定「接管后做什么」；
- 与现状关系：tier `exec` 是 `contract::RootProgram{kind, argv}` 的**泛化**；`script`/`ko` 为**新增**能力。

## 2. 字段与拼写（r2）

~~~
payload {
  tier = "exec" | "script" | "ko"        # 字符串 token；P1 单档互斥
  exec   { command = "<argv 形式，≤256B>"  sha256 = "<64hex，可选>" }
  script { path    = "<相对路径，≤256B>"   sha256 = "<64hex，可选>" }
  ko     { count   = N (1..8)
           ko.0 { path = "..."  sha256 = "..." }
           ko.1 { path = "..."  sha256 = "..." } }
}
~~~

- wire 路径：`payload.tier`、`payload.exec.command`、`payload.exec.sha256`、`payload.script.path`、`payload.script.sha256`、`payload.ko.count`、`payload.ko.<i>.path`、`payload.ko.<i>.sha256`；
- **用语义词不用 t1/t2/t3**：`exec/script/ko` 自解释，UI 可显示为「一/二/三档」；
- **tier 缺失/为空 ⇒ payload 不启用**（默认关闭）：行为与今天逐字节一致；
- **单档互斥（P1）**：`tier=exec` 时出现 `script.*`/`ko.*` ⇒ 拒绝；组合留待另立设计（组合会同时放大失败语义与授权面）。

## 3. 多 ko 编码（方案 A，冻结）

- **索引键 + 显式 count**：`payload.ko.count ≤ 8`，条目 `payload.ko.<i>.{path,sha256}`（先序号后字段，成组可读）；
- **fail-closed 规则**：`count` 缺失/为 0/`>8` ⇒ 拒绝；实际出现的 `ko.<i>.path` 个数 ≠ `count` ⇒ 拒绝；出现 `i ≥ count` ⇒ 拒绝；`i` 非十进制或重复 ⇒ 拒绝；任一 `path` 非法（绝对/`..`/反斜杠/NUL/超长）⇒ 拒绝；`sha256` 存在但不是 64 位小写 hex ⇒ 拒绝；
- **方案 B（单个 manifest 文件）否决**：带外文件与「配置权威 = 文档」的单一权威原则冲突，且文件可信度要另立规则。

## 4. 三档语义、执行落点与失败语义

| tier | 语义 | 执行落点（依据） |
|---|---|---|
| `exec` | 以 root 执行一个命令/可执行程序（**argv 形式**，非 shell 拼接） | **root child**：`terminal/root_script.cpp` + `contract/identity.hpp` 的 `RootProgram::set_argv` + `backend/cve_2026_43499/terminal/root_child.cpp`（handoff 后执行） |
| `script` | 以 **LKM 通道**执行用户自带的脚本文件 | `terminal/umh_command.hpp`（UMH argv 规范）+ `backend/cve_2026_43284/lkm/lkm_image.*`（late-load 命令构造）+ LKM UMH 脚本路径 |
| `ko` | 加载 1..8 个 `.ko` | KernelSU late-load（经 root child 执行 `ksud`）或 root child 内 `finit_module`（实现时按可行性核实；`lkm_window.cpp` 只提供内核能力，不是加载器） |

### 4.1 统一失败语义（r2，取代原 T1 致命 / T2T3 非致命二分）

- **payload 失败绝不中断攻击链**（waiter/race/handoff 不受影响）——硬边界；
- **凡用户显式请求了 payload 而它未完成 ⇒ 本次运行的最终结论标为「未完成」**，并给**逐项原因**：`payload_error=<reason>`、`ko[i]=<reason>`；
- **同时保留提权状态记录**（`uid0` / KernelSU ready 照记）——既不谎报「全部成功」，也不让已完成的提权白费；
- `ko` 逐项执行：第 i 个失败记 `ko[i]=<reason>` 并**继续下一个**；全部成功 ⇒ payload 完成；
- 结果只用于结论与诊断，**不**改变控制流、不放松任何检查。

## 5. 可用性矩阵（payload × terminal，绑定期 fail-closed）

| terminal | `exec` | `script` | `ko` |
|---|---|---|---|
| `root_child` | 可用 | 不可用（无 LKM 通道） | 可用（经 root child 的 late-load） |
| `umh_forward` | 可用 | 可用 | 可用 |

- 上表以**代码实际能力**为准，实现批次逐项核实后钉死（与 plugin 的 `stage_availability` 同纪律）；
- 绑定期校验：`(terminal, tier)` 不在矩阵内 ⇒ **fail-closed 拒绝**；
- 导出方式沿用 `stage_availability` 的做法：**native 导出矩阵**（后续可加进探针/词汇 manifest），**Kotlin 不硬编码**，UI 用同一矩阵置灰并给原因。

## 6. 安全边界与授权面

| 维度 | 规则 |
|---|---|
| 路径 | 一律相对 `<GHOSTLOCK_HOME>`；禁绝对/`..`/反斜杠/NUL；realpath 二次校验；≤256B；形状规则可复用 `plugin_module_path_valid()`（根不同） |
| 哈希 | 每个 `path`/命令可选 `sha256`（64 位小写 hex）；给定时在执行/加载前逐字节比对（复用 `support::sha256_file`），不符 ⇒ fail-closed **且不执行** |
| 大小 | `stat` 大小上限后才读入（ko 建议 ≤64 MiB，与 43499 module 上限同量级；脚本/命令另行给限） |
| ko 内容 | **必须**过现有 `lkm::precheck_module_file`（ELF/vermagic/`__versions`/签名）；**不得**因「用户自定义」放宽 |
| **argv 与脚本的区别** | `exec.command` 是 **argv 形式**（native 按空白切分后以 argv 传递，**永不** `sh -c` 拼接、不做变量/通配展开）；`script.path` 指向**用户自带的脚本文件**，由 LKM 通道执行——**命令不是脚本**，脚本必须落文件并被哈希钉住，禁止把命令文本当脚本执行 |
| 授权面（App） | 每个 tier 一个专用设置页 + **确认文案** + 可撤销；**执行前摘要**必须显示「本次将：以 root 执行 `…` / 以 LKM 执行 `…` / 加载 N 个 ko」；`payload` 未提供 ⇒ **逐字节回归** |
| 不可信内容 | 用户提供的一切（命令、脚本、ko）不可信：结果只记账；不放宽任何既有校验 |

## 7. 与 terminal / ADR-0006 的关系

- terminal 词汇与「实现下放 backend」**不受影响**；payload 是**横切执行策略**，不属于 terminal 词汇、也不属于 backend 私有策略；
- **实现落点：先随 `root_child` 实现**，当第二个复用点出现（`umh_forward` 复用同一执行器）再上提到中性模块——与 ADR-0006 既有判据一致；
- 映射：`root_child × {exec, ko}`、`umh_forward × {exec, script, ko}`（见 §5 矩阵）。

## 8. 测试与真机门禁

- **host**：编码/解码矩阵（tier 枚举非法、单档互斥违反、`count` 与实到数不一致、`i ≥ count`、重复/非十进制 i、超长路径、控制字符、sha256 非 64hex）→ 全部 fail-closed；`(terminal,tier)` 矩阵外组合 → 拒绝；无 `payload.*` ⇒ 零行为（逐字节回归）；
- **真机（攻击关键路径，按 AGENTS 门槛）**：
  1. `exec` 正例：`/data/local/tmp` 下可执行 → PASS，`uid0`/KernelSU ready 记录保留，日志含 `payload=exec rc=0`，AVB 12/0；
  2. `script` 正例：脚本创建标记文件 → 日志含 `payload=script ok`；
  3. `ko` 正例：2 个 ko（1 好 1 坏）→ 好的驻留、坏的记 `ko[1]=<reason>`，**攻击链 PASS 但本次运行标记「未完成」**；
  4. 负例：sha256 不符 / 路径含 `..` / 超限 / 矩阵外组合 → 拒绝且**不执行**，攻击链 PASS 且标「未完成」；
  5. 回归：无 `payload.*` ⇒ 与今天一致（`cmp_disasm` 可选诊断）。

## 9. 裁决记录（Lead 2026-10-05，共 8 条 + 1 条澄清）

1. 段名 `payload` 顶层 owner ✓；2. 用语义词 `exec/script/ko`（不用 t1/t2/t3）✓；3. 多 ko = 方案 A（`ko.<i>.{path,sha256}`，count ≤8），方案 B 否决 ✓；4. P1 单档互斥 ✓；5. **统一失败语义**：不中断攻击链，但用户显式请求未完成 ⇒ 本次运行标「未完成」+ 逐项原因，同时保留提权记录 ✓；6. 落点先随 `root_child` ✓；7. payload×terminal 可用性矩阵显式 + 绑定期 fail-closed + native 导出（Kotlin 不硬编码）✓；8. 授权面（分档设置页 + 确认文案 + 可撤销 + 执行前摘要）✓；9. argv 与脚本区别写清 ✓。

**实现准入**：本设计经用户确认后由 Lead 放行；实现批次只碰 payload 相关文件。

