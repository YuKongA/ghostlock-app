# 插件「提取声明」设计：extractor 侧语义（反汇编/方法链）（L 级，设计稿待评审）

> 状态：**设计稿，不含实现**。本册 = **extractor 半场**：`spec` 行的解析语义、方法链执行、反汇编参数、失败语义与诊断。
> 列集权威 = native 半场 [`plugin-extract-spec-design.md`](plugin-extract-spec-design.md)：**§10（r3 冻结 16 列）+ §11（r4 收敛记录）**；本册只补充列内语义与执行规则，不另立列。
> 用户需求（原话）：「导入的插件不仅要声明配置文件项让 kotlin 解析并检查，还要为 extractor 声明需要的 offset 提取/反编译参数」。
> L 级约束：先设计 → Lead 评审 → 用户确认 → 才可实现；实现顺序 **native → Rust(本册) → Kotlin → 之后才可分发带 `spec` 的制品**。

## 1. 范围与已冻结上下文（Lead 裁决 2026-10-05）

1. **载体 = 新 kind `spec`**（additive），**不给既有 `extract` 行追加列**：`extract` 行 7 列与 golden/63 份语料逐字节不变；
2. **同名冲突**：同一 `name` 同时出现在 `extract` 与 `spec` ⇒ **拒整个模块**（无例外）；
3. **显式链不回退**：`methods` 显式列出时**只**按所列顺序尝试；`-` = **默认阶梯** `profile → btf → kallsyms → disasm`（native §9.2 冻结值）；
4. **诊断通道**：stderr + `--format json` 的**稳定具名字段**；**不进 wire**；App 本期只显示「已解析/未解析」，命中方法属开发者诊断；
5. **v1 范围**：capture 五类（`pc`/`imm`/`disp`/`reg`/`pcoff`）+ `pattern` 1–3 条指令 + 字节模式；**不做** delta/两命中点差值；
6. **落点唯一命名空间**：`spec.name` 的值只写 `plugin.<id>.extract.<name>`；
7. **参数一律显式列**；列名、顺序、缺省以 native §10 为唯一权威（§11 覆盖同名列），本册 §7 给出提取侧语义与收敛状态。

## 2. 方法链语义

**声明**：`methods` = 逗号分隔的有序列表，取值 `profile|btf|kallsyms|disasm`，**不得重复**（native §9.1：未知 token/重复 ⇒ `required=1` 拒模块、`required=0` 拒行并记账）。

**执行**：

- 按所列顺序尝试；**第一个产出值的即命中并短路**；
- 命中方法写入诊断（§4），证据含 anchor/pattern/hit/pc 等（§3.7）；
- **显式列表 = 只走列表**；`-` = 默认阶梯（冻结值，三端同批才可改）；
- **硬错误立即终止整条链**（spec 畸形、类型与 signed/width 矛盾）——不继续尝试后续方法（fail-closed，与 P2 一致）；
- **miss 才继续**（锚点缺失、`hits < hit`、捕获位形不匹配）；
- 全链失败：`required=1` ⇒ profile 生成失败（exit 4，不写 `--out`）；`required=0` ⇒ 省略该键 + 诊断；**default 绝不顶替**（沿用 P2 D4 裁决）。

### 2.1 各方法对 `name` 的解释（显式化，取消名字嗅探）

| method | `name` 语义 | 取值 |
|---|---|---|
| `profile` | owner-qualified profile 路径（白名单 = `conf_wire_fields()`） | 同一份 profile 的字面量（P2 的 R1） |
| `btf` | `struct.<s>.<f>` 或 `sizeof.<s>` | 字节偏移 / 结构大小（P2 的 R3） |
| `kallsyms` | 内核符号名（**不**嗅探点号） | `unique(symbols,name) - image_base`（P2 的 R2） |
| `disasm` | **不透明键**（值由 §3 参数决定） | §3 |

### 2.2 向后兼容

- `extract` 行（无 spec）行为**与 P2 逐字节一致**：隐式阶梯 `profile → btf → kallsyms`；
- `spec` 行带 `methods=-` 时走默认阶梯（含新增的 disasm 段；旧插件无 spec 行，故无影响）；
- 混用允许（逐行独立）；输出键集合 = 合并后的 `plugin.<id>.extract.*`。

## 3. 反汇编参数（`spec` 的 disasm 语义）

### 3.1 `anchor`（锚点）

| 形式 | 语义 | 失败 |
|---|---|---|
| `sym:<name>` | kallsyms 唯一符号 → 镜像内偏移（复用 `unique_offset`） | 缺失/多值 ⇒ miss（具名） |
| `path:<profile-path>` | 取该 profile 字段的数值作锚点（与 R1 同源） | 未发射/null/非数值 ⇒ miss |
| `pc:<hex>` | 直接给镜像内偏移（离线复现/测试） | 越界 ⇒ miss |
| `-` | 无锚点；**仅与 `scope=text` 同用合法** | 与 `scope=anchor` 同用 ⇒ 硬错误；`disasm` ∈ methods 而缺省 ⇒ 拒行/拒模块（native §10.2） |

### 3.2 `scope`（扫描范围，含「必须落在函数内」约束）

| scope | 窗口 |
|---|---|
| `anchor`（缺省） | `[anchor_addr, min(下一个符号偏移, anchor + max_scan))`——复用 `disassemble_symbol` 的 next-symbol 界 |
| `text` | `[0, min(kernel_len, max_scan))`，必须显式声明（防全镜像扫描）；methods 含 `btf/kallsyms` ⇒ 拒行（native §10.2/§11.7） |

对齐固定 4 字节（A64 指令长度），不作为参数暴露。

### 3.3 `pattern`（匹配语义：作用在**解码结果**上）

前缀二选一（native §10.1 第 9 行 + §11.1 细化，**已收敛**）：

1. `bytes:<hex>`：**精确字节序列**；`??` = **恰好一个字节**通配（不接受半字节通配）；hex 串长度必须是 **4 的倍数**（否则拒行）；按 4 字节对齐匹配；
2. `insn:<1..3 条，以 `;` 分隔>`：**严格相邻**的指令谓词序列；每条 = 助记符 +（可选）操作数谓词：
   - 助记符：小写、精确匹配 `Opcode`（如 `cmp`、`ldr`、`adrp`）；
   - 操作数谓词（最小集）：字面寄存器 `x0/w1/sp/xzr/wzr`；寄存器类 `x?`/`w?`；立即数 `#?`（任意）/ `#0x1234`（字面）；内存 `[x?, #?]`、`[sp, #?]!`、`[sp], #?`；`?` = 任意单操作数。

**禁止**：正则、nibble 通配、跨指令模糊匹配；**不得**对 `format!("{instr}")` 的渲染文本做字符串/正则匹配（native §4/§11.1）。

**实现**：直接读 `yaxpeax_arm::armv8::a64::Instruction { opcode, operands[4] }`，按 `Operand::{Register, Immediate, ImmShift, PCOffset}` 逐位匹配。

**已知边界**：crate 无 mnemonic 反查表（`Opcode` 只有 `Display`），拼错的助记符**无法预校验**，表现为 `hits=0` 的具名 miss（不静默）。

### 3.4 `hit`（命中序号）

- 十进制 1..64，**缺省 1**（`-`）；`0` 非法（native §10.2）；
- 命中总数 `hits` 计入证据；`hits < hit` ⇒ miss（`hits=0`、`hits=2 < 3`）；
- `pattern=-`（无模式，native §10.1）时**本册定义**（匹配属 extractor 侧）：`hits` = scope 内自起点起的候选指令总数，`hit=N` 选中第 N 条（即 `[start + 4*(N-1)]` 处的指令）；`capture=-` 时为该指令镜像内地址。

### 3.5 `capture`（捕获）

语法（native §10.1 第 11 行）：`[<insn_index>.]<kind>:<operand_index>`；`insn_index` 为模式内 1-based 指令序号，缺省 = 模式**最后一条**；`kind` 的最终集合见 D2 状态。

| kind | 含义 | 备注 |
|---|---|---|
| `pc` | 命中指令自身的镜像内地址 | 无 operand_index；`capture=-` 即此值（native §11.3） |
| `imm` | 立即数操作数 | `Immediate`/`ImmShift` 均接受 |
| `disp` | 内存操作数的位移 | 只接受内存位形 |
| `reg` | 寄存器号（0..31） | `Register`/`RegisterPair` |
| `pcoff` | PC 相对目标（`Operand::PCOffset`）解析后的绝对目标 | 用于 `bl`/`b`/`adr` |

操作数位形不匹配（如声明 `imm` 但该操作数是寄存器）⇒ **miss**（不是硬错误）：同一助记符在不同内核上可有不同操作数形态（`mov w0, #1` vs `mov w0, w1`）。

> **D2（已裁决，native §11.2 r5）**：kind 集合冻结为 **`pc|imm|disp|reg|pcoff`**，删除 `symbol_va`（一个概念一个词）。`pc` = 命中指令自身镜像内地址；`pcoff` = PC 相对目标解析后的目标（`bl`/`b`/`adr`）；`imm`/`disp`/`reg` 同前；与解码器操作数模型一一对应（`Register`/`Immediate`/`ImmShift`/`PCOffset`）。

### 3.6 `width` / `signed` / `base` / `max_scan`

| 列 | 取值 | 缺省 | 语义 |
|---|---|---|---|
| `width` | `auto\|1\|2\|4\|8`（**字节**） | `auto` | 立即数取值宽度 |
| `signed` | `0\|1` | `0` | `1` 时按 `width` 做符号扩展 |
| `base` | `image\|anchor\|raw` | `image` | `image` 减镜像基址（与 profile offset 同基准）；`anchor` 减锚点地址；`raw` 原样 |
| `max_scan` | 字节数（`0x` 或十进），1 .. **1 MiB** | `0x2000` | 超出 = 拒行（native §11.6 最终值） |

- **`width=auto`（native §11.4，已收敛）**：yaxpeax 的 `Immediate(u64)`/`ImmShift(u16,u8)` **不携带字段宽度**，故 `auto` = **解码值原样（u64，不做符号/零扩展）**；**`signed=1` 且 `width=auto`（或缺省）⇒ 拒该行**；需要符号扩展必须显式给 `1|2|4|8`；
- **`signed` 与 `type` 强一致（native §11.5，已收敛）**：`type=int` 且 `signed=0` ⇒ 拒行；`type=uint` 且 `signed=1` ⇒ 拒行；`type ∈ {str,bool}` 且 `signed=1` ⇒ 拒行；
- `width` 与 `type` 明显不符（如 `type=str` 配 `width=8`）⇒ 拒行（native §10.2）。

### 3.7 失败语义（全部具名）与证据

| 情形 | 结果 |
|---|---|
| anchor 缺失/多值/越界 | miss（`anchor ... not unique/missing/out of range`） |
| 解码失败（字面量池等数据字） | 前进 4 字节继续；记 `undecoded=K`；窗口耗尽仍无命中 ⇒ miss |
| `hits < hit` | miss（`hits=K`） |
| 捕获位形不匹配 | miss（`operand ... is not <kind>`） |
| `pattern`/`scope`/`base`/`width` 语法非法 | **硬错误**（spec 畸形，立即终止链） |
| `signed` 与 `type` 矛盾（含 `signed=1 + width=auto`） | **硬错误** |
| `scope=anchor` 但无 `anchor`；`base=anchor` 但无 `anchor` | 拒行（native §10.2/§11.7） |

证据（命中时）：`anchor=<spec> scope=<...> pattern=<...> hit=i/hits pc=0x... capture=<kind>[:op] width=<...> signed=<...> base=<...>`；未命中：`name -> - reason=<...>`。

### 3.8 最小集够用性验证（仓库内 5 类真实推导）

| 现有内部推导（`derive.rs`/`disasm.rs`） | 等价插件声明（`spec` 语义） |
|---|---|
| `first_sp_frame` 的 preindex 项 | anchor=`sym:do_ipv6_setsockopt`；scope=anchor；pattern=`insn:stp x29, x30, [sp, #?]!`；capture=`disp:2`；base=raw；width=8 |
| `add_sp_immediates` | pattern=`insn:add x?, sp, #?`；capture=`imm:2`；hit=N |
| `cmp_immediates`（pselect 阈值） | pattern=`insn:cmp x?, #?`；capture=`imm:1`；hit=N |
| `materialized_address`（adrp+add） | pattern=`insn:adrp x?, #?; add x?, x?, #?`；capture=`2.imm:2`；base=image |
| `bl_targets`/`has_direct_call` | pattern=`insn:bl #?`；capture=`pcoff:0`；base=image |

=> 五类真实用例均可由本册最小集表达；v1 不需要更复杂语法。

## 4. 诊断（§1.4 裁决的落地形态）

- **stderr**：每个 `spec` 一行 `plugin <id> extract <name> -> <method>`（**命中**方法）或 `-> - reason=<...>`；
- **`--format json`**：稳定具名字段（App 本期只看 `resolved`；字段名待冻结）：

```json
"plugin_extract": [
  {"id": "glk.probe", "name": "platform.abi.offset.init_task", "method": "profile", "resolved": true,
   "evidence": {"path": "platform.abi.offset.init_task"}},
  {"id": "glk.probe", "name": "target_va", "method": "disasm", "resolved": true,
   "evidence": {"anchor": "sym:__arm64_sys_futex", "pattern": "insn:cmp x?, #?", "hit": 2, "hits": 3,
                "pc": "0x1a2b4", "capture": "imm:1", "width": 4, "signed": 0, "base": "image"}},
  {"id": "glk.probe", "name": "missing_thing", "method": null, "resolved": false, "reason": "hits=0"}
]
```

- **wire 不携带**诊断；HOCON 也**不写**注释留痕（避免 App 再渲染时丢失造成错觉）。

## 5. 可行性（证据）

- **已有解码器**：`yaxpeax-arm 0.5` + `yaxpeax-arch 0.3` 已在 `Cargo.toml`，`derive.rs`/`analysis.rs` 已在用，**已链接进二进制**；**无需新依赖、无需外部进程、完全离线**；
- **许可证**：`yaxpeax-arm 0.5.0` 的 `Cargo.toml` 声明 `license = "0BSD"`（crates.io 缓存源码包核实），与仓库 Apache-2.0 兼容；
- **结构化匹配可得**：`Instruction{opcode, operands[4]}`、`Operand::{Register, Immediate, ImmShift, PCOffset}`、`Opcode: Display`（小写助记符）；`disassemble_range` 已做 PC 相对重写；
- **可复用件**：`disassemble_range`、`disassemble_symbol`（next-symbol 界）、`relative_symbols`/`unique_offset`、`OBJDUMP_CAP=0x2000`；新增 = spec 解析 + 结构化 matcher + capture（估 400–600 行 + 单测）；
- **需同步的小改（native §11.6 要求）**：`disassemble_range` 现硬上限 `0x20000`，需放宽到 `max_scan`（≤1 MiB）；内部其它调用点维持各自 cap；
- **边界**：① 无 mnemonic 反查表（拼错 ⇒ `hits=0`）；② 操作数序号要求作者了解 AArch64 操作数序（文档给常用形状表）；③ 数据字面量池 → `undecoded` 计数；④ release 已是 `opt-level=z + lto + strip`。

## 6. 实现批次（与 native 冻结顺序一致）

1. native：ABI V3 尾部 + `spec` kind + 自断言（native-core，`92fe9e32`/`00443e6e`）→ 提交；
2. **Rust extractor（本册）**：解析 `spec` 行 → 方法链执行 → disasm 引擎 → 诊断（stderr + JSON）→ 单测（含 §3.8 五类）+ `spec` 语料用例；
3. Kotlin：认识 `spec`（模型/校验/只读展示）；
4. 2、3 落地并验证后，才允许分发/导入带 `spec` 的制品。

## 7. `spec` 行语义（列集以 native §10/§11 为唯一权威）

| 列 | 取值域（native §10.1/§11） | 缺省 `-` | 提取侧语义/失败（本册） |
|---|---|---|---|
| `methods` | `profile\|btf\|kallsyms\|disasm`，逗号有序、无重复 | 默认阶梯 | 只走列表；短路；硬错误终止；miss 继续（§2） |
| `anchor` | `sym:` / `path:` / `pc:` | 无（disasm 必需） | §3.1 |
| `scope` | `anchor\|text` | `anchor` | §3.2 |
| `pattern` | `bytes:<hex ??>` / `insn:<1..3 ; 分隔>` | 无模式 | §3.3（已收敛） |
| `hit` | 1..64 | `1` | §3.4 |
| `capture` | `[i.]<kind>:<op>`，kind ∈ `pc\|imm\|disp\|reg\|pcoff`（冻结） | `pc`（native §11.3） | §3.5 |
| `width` | `1\|2\|4\|8\|auto` | `auto` | §3.6（§11.4 已收敛） |
| `signed` | `0\|1` | `0` | §3.6（§11.5 已收敛） |
| `base` | `image\|anchor\|raw` | `image` | §3.6 |
| `max_scan` | 1 .. 1 MiB | `0x2000` | §3.6（§11.6 已收敛） |

### 7.1 与 native §10/§11 的收敛状态

| # | 议题 | 状态 |
|---|---|---|
| D1 | `pattern` 前缀与细化（`bytes:`/`insn:`、`??` 一字节、4 的倍数） | **已收敛**（native §11.1） |
| D2 | `capture` kind 集合 = `pc|imm|disp|reg|pcoff`（删 `symbol_va`） | **已裁决冻结**（Lead 2026-10-05；native §11.2 r5） |
| D3 | `capture=-` 语义 = `pc` | **已收敛**（native §11.3） |
| D4 | `width=auto` = 原样 u64；`signed=1 + auto` 拒行 | **已收敛**（native §11.4） |
| D5 | `type=int & signed=0`、`type=uint & signed=1` 拒行 | **已收敛**（native §11.5） |
| D6 | `max_scan ∈ [1, 1 MiB]`，缺省 `0x2000`；Rust 放宽内部守卫 | **已收敛**（native §11.6） |

## 8. 待裁决 / 收敛中

1. ~~D2（capture kind 集合）~~ —— **已裁决冻结**（`pc|imm|disp|reg|pcoff`）；D1–D6 全部收敛，Rust 侧实现前置已齐备；
2. JSON 诊断字段名（§4 为草案，App 将来消费需冻结）；
3. `spec` 语料用例入仓时点：按 §9.5 顺序，**native + Rust 落地后**再进语料（不在本批）。

## 9. 实现补充（Rust 落地，2026-10-05）

实现位于 `tools/extract_rs/src/spec.rs`（引擎）+ `plugin.rs`（~~spec~~ kind 接入）+ `main.rs`（诊断/输出），集成测试 `tools/extract_rs/tests/extract_spec.rs`。落地时确认/细化的语义：

1. **助记符别名（实现发现，必须写清）**：yaxpeax 的 `Instruction.opcode` 是**规范 opcode**（`cmp` 解码为 `subs`），而渲染文本给出别名。匹配规则 = 模式助记符等于 **opcode 名或渲染文本的首个 token**；**操作数一律结构化匹配**（`Operand` 变体），从不对渲染文本做字符串/正则匹配。
2. **隐式操作数与对齐**：别名 `cmp`/`cmn`/`tst` 在解码结果里带一个隐式零寄存器目的操作数。匹配允许「操作数个数 == 谓词个数 + 1 且首个操作数是零寄存器」时右移一位对齐；其余情况要求精确相等。无操作数谓词的模式（如 `ret`）只按助记符匹配。
3. **`capture` 的 `operand_index`** = **pattern 谓词序号**（0-based），不是解码操作数数组下标；`insn_index` 为模式内 1-based 指令序号，缺省 = 模式最后一条。
4. **内存操作数**：yaxpeax 用单个 `RegPreIndex(reg, disp, writeback)` / `RegPostIndex(reg, disp)` 表示 `[r, #d]`、`[r, #d]!`、`[r], #d`；模式里的 `!` 必须与实际 writeback 一致；`disp` 捕获只接受带位移的内存谓词。
5. **`scope=text` 与默认阶梯**：默认阶梯含 `btf/kallsyms`，因此 `methods=-` + `scope=text` 会命中 §10.2 的「语义冲突」规则而被拒——需要 `scope=text` 时必须显式列 `methods`（例如 `methods=disasm`）。
6. **诊断落点**：`--format conf` 时 stderr 打 `plugin <id> extract <name> -> <method>`；`--format text|json` 时同一批诊断以稳定字段 `plugin_extract` 数组进 JSON（`id/name/resolved/method/evidence/reason`），**不进 wire/HOCON**。json 模式下解析针对一个 **route-less candidate**（只有 conf 模式做 route 推断），因此 route 相关的 profile 路径只在 conf 模式可解析——这是本批的已知边界，端到端联调（native 出 `spec` 行）时可再统一。
7. **`--plugin-descriptor` 的格式面**：P2 曾限定仅 `--format conf`；本批放宽为任何 format（conf 写块，text/json 只出诊断），因为诊断必须能脱离 conf 输出单独查看。
8. **共享语料不动**：conformance 语料是「探针 TSV 的 verdict 语料」，`spec` 是新能力，其用例待 native 产出 `spec` 行后按 §9.5 顺序另批进仓。

