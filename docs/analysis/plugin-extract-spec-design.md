# 插件「提取声明」设计：ABI 尾部 + 探针 TSV 扩展（L 级，设计稿待评审）

> 状态：**设计稿，不含实现**。用户需求（原话）：「导入的插件不仅要声明配置文件项让 kotlin 解析并检查，还要为 extractor 声明需要的 offset 提取/反编译参数」。
> 本册范围 = **native 半场：C ABI 尾部追加 + 探针 TSV 语法扩展 + 兼容性/迁移顺序 + 探针自断言**；extractor 侧的解析/执行（Rust 投影）与 Kotlin 侧模型属各自批次，本册只给出它们必须遵守的接口与顺序。
> L 级约束：本设计交 Lead 评审 → 报用户确认 → 才可实现。

## 1. 现状与缺口

- 现有尾部 `extract: const glk_param *`（name/type/required/default/doc）只能表达「**要什么**」（一个名字 + 类型），不能表达「**怎么取**」；
- extractor 现有的阶梯（profile → btf → kallsyms → disasm）是**提取器内部隐式逻辑**；插件无法声明自己的锚点、模式、命中序号、捕获字段、位宽；
- 结果：插件作者无法表达「取 `struct.task_struct.pi_waiters` 的位移，方法优先 BTF，否则反汇编 `__futex_wait` 找 `ldr xN,[xM,#0x??]` 的第 2 个命中」这类真实需求。

## 2. 两类声明的分工（与用户解读一致）

| 类别 | 现有载体 | 消费者 | 语义 |
|---|---|---|---|
| **配置项** | 探针 `param` 行 / `glk_param.params` | Kotlin（解析/校验/编辑）→ `plugin.<id>.params.*` | 用户在 App 里能填什么 |
| **提取声明（新增）** | 探针 `spec` 行 / `glk_extract_spec.spec` | extractor（生成 profile 时解析）→ `plugin.<id>.extract.*` | 插件要什么值 + 用什么方法取 |

> 用户解读的三点（目标路径/符号、方法与回退顺序、反编译参数、required 语义）**完全一致**，无偏差；补充一条：`required` 未解析 ⇒ 整个生成失败（**绝不用 default 顶替**），optional 未解析 ⇒ 省略 + 诊断。

## 3. C ABI：尾部追加（append-only，不 bump `GLK_ABI_VERSION`）

~~~c
/* 提取方法。位序 = 尝试顺序（低位优先），与 extractor 现有阶梯同序。 */
typedef enum glk_extract_method {
    GLK_EXTRACT_PROFILE  = 0,   /* 已有 profile 字段（owner-qualified 路径） */
    GLK_EXTRACT_BTF      = 1,   /* BTF: struct.<s>.<f> / sizeof.<s> */
    GLK_EXTRACT_KALLSYMS = 2,   /* 符号表: 符号名 */
    GLK_EXTRACT_DISASM   = 3    /* 反汇编/反编译: anchor + pattern + occurrence */
} glk_extract_method;

typedef enum glk_capture_field {
    GLK_CAPTURE_IMM       = 0,  /* 立即数 */
    GLK_CAPTURE_DISP      = 1,  /* 内存位移 */
    GLK_CAPTURE_REG       = 2,  /* 寄存器号 */
    GLK_CAPTURE_SYMBOL_VA = 3   /* 命中的符号虚拟地址 */
} glk_capture_field;

typedef struct glk_extract_spec {
    const char *name;       /* profile 路径 / 符号 / struct.<s>.<f> / sizeof.<s> */
    uint32_t    type;       /* glk_param_type（与 GLKv3 WireKind 同字面量） */
    uint32_t    required;   /* 0/1；未解析时 required=1 ⇒ 生成失败 */
    uint32_t    method_mask;/* OR of (1u << glk_extract_method)；0 = 走 extractor 默认阶梯 */
    const char *anchor;     /* disasm 锚点（符号/函数名）；其它方法可为 NULL */
    const char *pattern;    /* disasm 模式；NULL = 仅锚点 + occurrence */
    uint32_t    occurrence; /* 第几个命中，1 起；0 非法（显式优先） */
    uint32_t    capture_field; /* glk_capture_field */
    uint32_t    width;      /* 捕获位宽（字节）：1|2|4|8 */
    const char *doc;        /* 说明，可 NULL */
} glk_extract_spec;
~~~

- `glk_module` **再次尾部追加**：`uint32_t spec_count; const glk_extract_spec *spec;`，并新增 `#define GLK_MODULE_SIZE_V3 ((uint32_t)sizeof(glk_module))`；host 仅在 `module->size >= GLK_MODULE_SIZE_V3` 时读这两个字段——**旧模块（v1/v2 size）永不读新尾部**；
- **既有 9 个字段与 v2 尾部（param_count/params/extract_count/extract/stage_mask）的顺序、含义、数值全部冻结**；不 bump `GLK_ABI_VERSION`（新增能力靠「声明 + 忽略」）；
- **与旧 `extract` 的关系：并存**。旧 `extract` 语义不变（=「要什么」，方法走默认阶梯）；`spec` 是更精确的表达。**同名同时出现在 `extract` 与 `spec` ⇒ 拒绝整个模块**（避免两个权威）。

## 4. 探针 TSV：新增 kind `spec`（additive）

**`extract` 行保持 7 列不变**（golden 与 63 份语料已钉死）；新增 kind：

| kind | 列序（TAB 分隔） | 列数 |
|---|---|---|
| `spec` | **见 §10 冻结列集（16 列）** | 16 |

- `methods`：逗号分隔的**尝试顺序**列表，取值 `profile|btf|kallsyms|disasm`；空 ⇒ `-`（= 默认阶梯）；**不得出现重复方法**；
- `pattern` 编码与**匹配语义**（冻结）：token 以**单个空格**分隔，每个 token 二选一——① `0x` 前缀 = **精确字节序列**（如 `0xf9400260`）；② 其它 = **助记符前缀**（如 `ldr`），匹配**解码后指令**的助记符前缀；③ **禁止正则、nibble 通配、跨指令模糊匹配**；实现必须作用在**解码结果**上，**不得**对渲染出的反汇编文本做字符串/正则匹配（输出格式一变就脆断）；空 ⇒ `-`；
- `occurrence`：十进制 ≥1；`0` **非法**（禁用「首个」的隐式语义）；
- `capture`：`imm|disp|reg|symbol_va`；`width`：`1|2|4|8`；
- 空值一律 `-`；`doc` 可空；
- 行序：`plugin` → `hook` → `param` → `extract` → **`spec`** → `reject`（`spec` 紧跟 `extract`，保持「先配置、后提取」的阅读序）；
- **输出条件**：仅当模块声明了新尾部（`size ≥ GLK_MODULE_SIZE_V3` 且 `spec_count > 0`）才输出 `spec` 行 → **旧模块零影响**，未升级的消费者不会因此拒整份描述符。

## 5. fail-closed 规则（探针侧）

| 情形 | 处置 |
|---|---|
| `method_mask` 含未知位 / `methods` 出现词表外 token **且该行 `required=0`** | **拒该行 + 记账**（`reject` 行） |
| 同上**但该行 `required=1`** | **拒整个模块**（不得静默丢掉一个必需声明，否则会出现「提取器不知道要取、运行时却需要」的空洞） |
| `methods` 重复同一方法 | 拒该行 |
| `occurrence = 0` / `width ∉ {1,2,4,8}` / `capture` 词表外 | 拒该行 |
| `capture ∈ {imm,disp}` 但 `width` 与 `type` 明显不符（如 type=str） | 拒该行 |
| `name` 为 NULL/空/含控制字符/长度 > 64 | 拒该行（与 D7 同规） |
| `pattern` 非空但 > 256B，或含 TAB | 拒该行 |
| `disasm` 在 `methods` 中但 `anchor` 为 NULL | 拒该行（disasm 必须有锚点） |
| 同名同时出现在 `extract` 与 `spec` | **拒整个模块** |
| `spec_count > 32` 或 `spec == NULL` 且 `spec_count > 0` | 拒整个模块（越界/畸形表） |

## 6. 兼容性影响与迁移顺序（关键）

- **旧消费者的行为**：现契约「未知 kind → 按 header 解析 → 列数不符 → 拒整份描述符」⇒ 新增 `spec` 行会让**未升级**的 Kotlin/Rust 拒掉**整份描述符**；
- 因此 §4 的「仅新尾部模块才输出 `spec`」是**硬要求**：只要插件不声明新声明，旧消费者看到的就是与今天逐字节相同的 TSV；
- **三端同批升级顺序（硬约束）**：
  1. native：ABI 尾部 + 探针 `spec` kind + 自断言（本册）→ 提交；
  2. Rust extractor：认识 `spec` 行并在生成时按 `methods` 施阶梯（P2 投影）；
  3. Kotlin：认识 `spec` 行（模型/校验/UI 只读展示；不参与编辑）→ 之后再启用「带 spec 的插件导入」；
  4. 只有在 2/3 落地后，才允许分发带 `spec` 的新插件制品（否则用户导入即被拒）；
- 回归证据：`extract` 行 7 列不变 + 现有 golden 与语料逐字节不变；新 fixture 才带 `spec`。

## 7. 探针自断言（保证「声明的 spec 与 ABI 尾部一致」）

1. `size` 门控：`size < GLK_MODULE_SIZE_V3` ⇒ 完全忽略 `spec_count`/`spec`（**不读指针**）；v1/v2 模块的回归由既有 poisoned-tail fixture 覆盖；
2. 每行**由尾部表逐项生成**，探针内不得有手写的 spec 字面量；
3. 生成后自校验：行数 == `spec_count`（被拒的行只出现在 `reject`，且计入诊断）；未知 method/非法 occurrence/width → 拒行并记账（不静默跳过）；
4. `spec_count ≤ 32` 且超出即拒模块；`spec` 为 NULL 而计数非 0 ⇒ 拒模块；
5. 与 `extract` 同名冲突 ⇒ 拒模块（与 §3 一致）。

## 8. 待裁决

1. **未知 method：拒该行 vs 拒整个模块**——我推荐**拒该行 + 记账**（与 D4/D7 的「必需列缺失不发行、可选增强 fail-soft」一致；且允许新方法在不同 ABI 版本下前进兼容）；若你更看重「探针通过 = 注册期通过」的强等价，则改为拒模块。
2. `pattern` 是否允许助记符前缀 token（`ldr`）——我推荐允许（反汇编在不同编译器下立即数不同，但助记符稳定）；若你要求「只用精确字节」，则移除该形式。
3. `method_mask = 0`（= 默认阶梯）与「显式列出四种方法」是否语义等价——我推荐**不等价**：0 表示「交给 extractor 的默认阶梯（未来可能演进）」，显式列出表示「只按我列的试」。
4. 是否需要 `constraint`（如「命中必须落在函数 X 内」）——我建议 P1 **不做**，先用 `anchor + occurrence` 覆盖；真实需求出现再加尾部字段。


## 9. 裁决落实（r2，Lead 2026-10-05，7 条）

### 9.1 未知 method 的两分支规则（取代 §8 第 1 问）

- `required = 0` 的行：**丢弃该行 + `reject` 记账**（与 hook/param 的逐行 fail-closed 先例一致）；
- `required = 1` 的行：**拒整个模块** —— 默默丢掉必需声明会造出「提取器不知道要取、运行时却需要」的静默空洞，必须让用户看见。

### 9.2 `methods = -` 的**默认阶梯**（显式写死，消除隐式）

`-`（或 `method_mask = 0`）表示按 extractor 的默认阶梯依次尝试，顺序冻结为：

~~~
profile  ->  btf  ->  kallsyms  ->  disasm
~~~

- 与「显式列出」**不等价**：显式列表 = **只**按所列顺序试，未列的方法**绝不**尝试；
- `-` 的阶梯是**本设计冻结的值**，将来若演进（例如插入新方法）必须改本节并同批升级三端，不得静默改变 `-` 的含义；
- `required` 语义对两种写法一致：阶梯/列表全部失败且 `required=1` ⇒ profile 生成失败；`required=0` ⇒ 省略 + 诊断。

### 9.3 解析结果的落点（唯一命名空间）

- `spec.name` 解析出的值**只**写到 `plugin.<id>.extract.<name>`（与 P2 的 extractor 投影**同一命名空间**）；**不得**另起 `plugin.<id>.spec.*` 之类的新命名；
- 同名冲突：同一 `name` 既有 `extract` 行又有 `spec` 行 ⇒ **拒整个模块**（§3 已定，不再有例外）；
- 两条来源（旧 `extract` 与 `spec`）合并后，`plugin.<id>.extract.*` 的键集合仍是最终权威。

### 9.4 诊断必须记录「实际命中的方法」

- 每个 `spec` 在生成诊断中输出一行 `name -> profile|btf|kallsyms|disasm`（**命中**的方法，而非声明的方法）；
- 未命中时输出 `name -> - reason=<...>`；
- 这是事后取证与「为什么取不到」的唯一线索；Rust 侧的对应要求已由 Lead 同步给 extractor-rs。

### 9.5 分发顺序 = 实现批次的准入条件（冻结）

1. native：ABI 尾部 + 探针 `spec` kind + 自断言（本册）→ 提交；
2. Rust extractor：认识 `spec` 并按 §9.2 语义执行 + §9.4 诊断；
3. Kotlin：认识 `spec`（模型/校验/只读展示）；
4. **只有 2、3 都落地并验证后**，才允许分发/导入带 `spec` 的插件制品。

**显式禁止**：制品先于消费者（用户导入即被整份拒绝）；**禁止**任何一端在未完成自身上一步时先行发布带 `spec` 的样例或语料。

### 9.6 `constraint`

- P1 **不做**（维持 §8 第 4 问的裁决）；先用 `anchor + occurrence` 覆盖；将来不足时按 append-only 规则扩展尾部字段与 `spec` 列（整列追加并同批改本节）。

## 10. `spec` 行**冻结列集**（r3，取代 §4 的 12 列草案；三方同一份）

> 裁决：用**显式列**（不采用 key=value 迷你语言），逐列 fail-closed；token 语法沿用 extractor-rs §2；`hit` 取代 `occurrence`（1-based，上限 64）。

### 10.1 列序（TAB 分隔，冻结；16 列）

~~~
kind  id  name  type  required  methods  anchor  scope  pattern  hit  capture  width  signed  base  max_scan  doc
~~~

| # | 列 | 取值域 | 缺省 `-` 的含义 |
| — | **注：第 9/11/12/13 行的取值域以 §11（r4 收敛）为准** | | |
|---|---|---|---|
| 1 | `kind` | 字面量 `spec` | — |
| 2 | `id` | 插件稳定 id（同 `plugin` 行） | — |
| 3 | `name` | 非空、无控制字符、≤64B；**解析结果写到 `plugin.<id>.extract.<name>`** | — |
| 4 | `type` | `uint|int|bool|str`（与 GLKv3 WireKind 同字面量） | — |
| 5 | `required` | `0|1` | — |
| 6 | `methods` | 逗号分隔、**即尝试顺序**，成员 ∈ `profile|btf|kallsyms|disasm`；无重复 | `-` = 默认阶梯 `profile→btf→kallsyms→disasm`（§9.2） |
| 7 | `anchor` | `sym:<name>` / `path:<profile-path>` / `pc:<hex>` | `-` = 必需（`disasm` 在 methods 中时不得缺省） |
| 8 | `scope` | `anchor|text` | `-` = `anchor` |
| 9 | `pattern` | `bytes:<hex 带 ?? 通配>` / `insn:<1..3 条，以 ; 分隔>`；禁止正则 | `-` = 无模式（仅 anchor + hit） |
| 10 | `hit` | 十进制 1..64 | `-` = `1` |
| 11 | `capture` | `[<insn_index>.]<kind>:<operand_index>`，`kind ∈ pc|imm|disp|reg|pcoff`（§11.2 裁决） | `-` = `pc`（命中指令镜像内地址，§11.3） |
| 12 | `width` | `1|2|4|8` 或 `auto` | `-` = `auto`（由 `type`/`capture` 推导） |
| 13 | `signed` | `0|1` | `-` = `0`（无符号扩展） |
| 14 | `base` | `image|anchor|raw` | `-` = `image` |
| 15 | `max_scan` | 十六进制（`0x…`）或十进制，1..1 MiB | `-` = `0x2000` |
| 16 | `doc` | 自由文本（无 TAB） | `-` = 无说明 |

### 10.2 fail-closed 规则（逐列）

- 列数 ≠ 16 ⇒ 拒该行（整份描述符的列数一致性仍由消费侧的既有规则保证）；
- `name/type/required` 任一缺失或非法 ⇒ 拒该行；`name` 重复（同一 id 内）⇒ 拒该行；
- `methods`：成员词表外/重复 ⇒ **`required=0` 拒该行 + 记账；`required=1` 拒整个模块**（§9.1 两分支）；
- `anchor`：`disasm` ∈ methods 且 `anchor` 为 `-` ⇒ **`required=1` 拒模块 / `required=0` 拒该行**；`sym:`/`path:`/`pc:` 前缀外 ⇒ 同上；
- `scope`：词表外 ⇒ 拒该行；`scope=text` 而 methods 含 `btf/kallsyms` ⇒ 拒该行（语义冲突）；
- `pattern`：`bytes:`/`insn:` 前缀外、`insn:` 超过 3 条、`bytes:` 非十六进制或含非法通配 ⇒ 拒该行；`insn:` 中的助记符按 §4 的**解码后指令**语义匹配（禁正则/跨指令）；
- `hit`：<1 或 >64 或非十进制 ⇒ 拒该行；
- `capture`：语法不符、`insn_index` 超出 `insn:` 条数、`operand_index` 越界 ⇒ 拒该行；
- `width`：非 `1|2|4|8|auto` ⇒ 拒该行；`width` 显式值与 `type` 冲突（如 `type=str` 且 `width=8`）⇒ 拒该行；
- `signed`：非 `0|1` ⇒ 拒该行；`type=str`/`type=bool` 且 `signed=1` ⇒ 拒该行；
- `base`：词表外 ⇒ 拒该行；`base=anchor` 而 `anchor=-` ⇒ 拒该行；
- `max_scan`：非法数值或 > 1 MiB ⇒ 拒该行；需要更大扫描范围的用例按 append-only 提新字段，**不得**放宽上限；
- 与旧 `extract` 行同名 ⇒ **拒整个模块**（§9.3，无例外）。

### 10.3 缺省值来源（唯一权威）

- 上表「缺省 `-` 的含义」列即**唯一权威**；缺失时探针**必须写出 `-`**（不代替 extractor 填值），extractor 按本表解析；
- 缺省值**只影响解析方法**，**绝不**为「提取值」提供默认（§1 补充条：给提取值默认等于撒谎）；
- 本表与 extractor-rs §2 的定义必须逐字一致；任何一方要改，先改本设计并同批通知三方。

### 10.4 与 ABI 的对应

- 16 列逐列对应 `glk_extract_spec` 的字段（`hit` ↔ `occurrence` 字段名保留但语义为 1-based ≤64；新增 `scope/signed/base/max_scan` 四字段进结构体尾部）；
- 结构体与 `spec` 行**同源生成**：探针从 ABI 尾部逐字段打印，不做任何列级重排或推导；
- 需要新增列时：整列追加 + 同批改本节 + 三端同批升级（§9.5 准入条件不变）。

## 11. 与 extractor-rs 的收敛记录（r4，2026-10-05）

对方逐字核对：12 项一致；6 项差异按下表收敛。**§11 的取值域覆盖 §10.1 同名列**，其余以 §10 为准。

### 11.1 D1 `pattern` 细化（采纳对方细化，值不变）

- `bytes:<hex>`：`??` = **恰好一个字节**通配（**不接受半字节通配**）；hex 串长度必须是 **4 的倍数**（否则拒行）；
- `insn:<1..3 条，以 ; 分隔>`：每条 = **助记符 + 可选操作数谓词**；
- 匹配仍作用在**解码后指令**上（§4），禁正则/跨指令/对渲染文本匹配。

### 11.2 D2 `capture` kind 集合 —— **已裁决（Lead 2026-10-05）：选项 ①**

- **冻结值：`kind ∈ {pc, imm, disp, reg, pcoff}`**，**删除 `symbol_va`**；
- `pc` = 命中指令自身的镜像内地址；`pcoff` = PC 相对目标解析后的目标地址（用于 `bl`/`b`/`adr`）；两者不重叠；
- 理由（Lead）：该集合与解码器操作数模型一一对应（`Operand::{Register, Immediate, ImmShift, PCOffset}`）；`symbol_va` 无独立语义（要么等于 pcoff、要么是同一概念的第二个名字，本项目禁止「两个词一个概念」）；
- 消费端词表以此为唯一权威，**不得**再出现 `symbol_va`；实现批次按此写死并与三端对拍；
- 扩展方式：将来若需要新 kind，按 append-only（整列/词表扩展 + 同批改本节 + 通知三方）。
### 11.3 D3 `capture = -` 的语义（采纳对方建议 ①）

- `-` = **`pc`**（命中指令自身的镜像内地址）——最通用且可判定；**不再**表述为「捕获整个命中」；
- 需要其它字段时必须显式写 `capture`。

### 11.4 D4 `width = auto` 的推导（采纳对方规则，可判定）

- `auto` = **按解码值原样（u64），不做符号/零扩展**；
- 因此 **`signed=1` 且 `width=auto`（或缺省）⇒ 拒该行**；需要符号扩展必须显式给 `width ∈ {1,2,4,8}`；
- 理由：yaxpeax 的 `Operand::Immediate(u64)` / `ImmShift(u16,u8)` **不携带字段宽度**，「解码器原始宽度」在 auto 下不可得——写成可判定规则而非猜测。

### 11.5 D5 `signed` 与 `type` 的一致性（采纳对方加强）

- `type=int` 且 `signed=0` ⇒ **拒该行**（否则负数被当无符号大数写入 int 槽，自相矛盾）；
- `type=uint` 且 `signed=1` ⇒ **拒该行**；
- 既有：`type ∈ {str,bool}` 且 `signed=1` ⇒ 拒该行。

### 11.6 D6 `max_scan` 上限（我方确认为最终值）

- **1 .. 1 MiB（0x100000）**，缺省 `0x2000`；对方接受并把 Rust 侧扫描上限提到 `max_scan`（内部其它调用点维持各自 cap）；
- 需要更大范围时按 append-only 提新字段，**不得**放宽上限（§10.2 不变）。

### 11.7 对方原稿没有、双方已同意的两条（我方 §10.2 既有）

- `scope=text` 且 methods 含 `btf/kallsyms` ⇒ 拒行（语义冲突）；
- `base=anchor` 而 `anchor=-` ⇒ 拒行。

### 11.8 后续动作

- Lead 裁决 D2 后：改 §11.2 → 通知 extractor-rs / kotlin-app（三方同一份）；
- Rust 侧实现前置：§11.1/11.3/11.4/11.5/11.6 均已可直接实现（D2 除外）；
- 语料新增 `spec` 用例仍按 §9.5 顺序（native 提交 + Rust 落地之后）进仓。
