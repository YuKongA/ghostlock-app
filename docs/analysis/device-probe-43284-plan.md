# 最小临时写路径（43284 真机探针）设计稿

> 2026-10-09 · Lead · 状态：待认可（L 级改动，按规矩先设计后动手）
>
> 本件是**待批事项 1**（见 `docs/analysis/pending-rulings.md`）。命令级清单见 `device-probe-43284-checklist.md`。

## 1 为什么需要它（实测依据）

目标要求「通过任何临时方法测试其在真机可用」。实测表明这条路**必须**是临时的：

- `core/src/main.cpp` 只 include 契约/内存/档案/设备事实/LKM 模块事实，**没有任何写路径**；
- 整个 `core/src/pipeline/` 里对 43284 的唯一引用是 `access_factory.cpp`（工厂）；
- 结论：新树是骨架——部件齐、各自有 host 测试，但**没有把 43284 的一次写接成一条可执行的路**。

而按本仓规矩，真机判据是攻击路径改动的唯一权威，且要求归档。于是本稿给出**最小**的一条路。

## 2 最小形态（不用旗标群）

两条本仓硬约束决定了形态：**新实验必须从生产路径启动**、**禁止为试功能新增一排 CLI 旗标与旁路**。因此：

1. 新增**一个**组合文件（`core/src/pipeline/probe_43284.cpp`），内容是**最小组合根**：造 `Access`（经工厂）→ `initialize`（参数里带 SA 材料与偏移）→ 一次 `write` → `shutdown`；
2. 入口**不是**新旗标：由现有的 `--ghostlock-app-call` 文档通道驱动——文档里选 `backend.cve_2026_43284`，探针**只在**该选择下生效；未选它时一个字节都不多；
3. 探针的**唯一**输出是**具名诊断**（成功/失败 + 哪一步），不打印任何密钥材料。

## 3 SA 材料的注入（绝不落盘）

SA 材料（SPI/端口/密钥）**不在仓库**，这是刻意的。注入路径 = 会话帧（现有设计），要求：

- 材料经**长度前缀的会话帧**进入，**不写文件、不进日志、不进 profile**；
- 用后**清零**（与 `PageCacheWrite::zeroize` 同一规矩）；
- 探针**不打印**材料，也不打印长度之外的任何派生量。

## 4 判据（设备上可用指什么）

1. **成功**：探针报告一次写完成，且设备日志能对应到这次写；
2. **失败**：必须**具名**指出哪一步，而**不需要新增状态**——四个具名状态**已经存在**：`Status::NotInitialized`、`WriteOutcome::NotOpen`、`WriteOutcome::NoOldBlock`、`WriteOutcome::NotSent`。（初稿还列了「校验被拒」，**实测不成立**：`compute_icv` 返回 `void`，`esp_icv` 只在 `icv_len == 0`、`> 32` 或空指针时拒绝，而这些都是构造期已知的。⇒ 写作时要少列一个状态。）
3. 归档：按 `docs/analysis/device-gates/` 的格式留证（含 uname、页大小、时间、构建标识）。

## 5 清理计划（临时物，同批登记，不得跨批次）

| 临时物 | 位置 | 删除条件 |
| --- | --- | --- |
| 探针组合文件 | `core/src/pipeline/probe_43284.cpp` | 设备判据归档后立即删除，并核对 Makefile 无残留目标 |
| 探针测试 | `core/src/tests/probe_43284_test.cpp` | 同上 |
| 文档通道里的探针分支 | 组合根/入口内的条件分支 | 同上；删除后 `grep probe` 应为 0 |

⇒ 判据一旦归档，本表即成为**执行清单**：逐条删除、逐条核对、提交信息写明删除完成。

## 6 我明确不做的事

- **不**新增 `--probe-*` / `--cve43284-*` 之类旗标（本仓为这类先加后删付过代价）；
- **不**把 SA 材料写进任何文档、测试固定值或提交（运行时密钥绝不进文档）；
- **不**为了能跑而放宽既有守卫（缺事实仍按 `write_readiness` 具名拒绝）。

## 3b 一处待搬的权威：SA 帧的字节布局（实测，含定量）

`SaMaterial`（`access_params.hpp:26`）有字段：spi · encap_port · sender_port · icv_len · aes_key[32] · hmac_key[32]。

**更正一处说法**：我先前写这句布局没有权威，不准确。权威存在，只是在旧树里、新树尚未搬：

- 旧件 `src/core/backend/cve_2026_43284/session_frame.hpp`（95 行）与 `.cpp`（174 行）**合计 269 行**；
- 它的文件头**已经写好字节布局**（+13 reserved2[3] · +16 aes_key[32] · +48 hmac_key[32] …），另有版本号、kind、trailer 三个常量；
- 也就是说：这件事不需要我设计布局，只需要**把它搬过来**。

| 选项 | 体量 | 契约归属 |
| --- | --- | --- |
| B 先搬 session_frame | 269 行，且布局现成 | 权威在 43284 自己的线格式件里，生产与探针共用，探针删掉也不留空白 |
| A 探针内定义 | 十几行 | 权威在探针里，而探针按计划要删，删完布局没有权威 |

⇒ 三条理由指向 **B**：帧本就属 43284 一侧；临时物不得承载契约；且它的布局表现成。

## 7 已核实的依据（六处，全部实测，供执行时对照）

| # | 事实 | 位置 / 证据 |
| --- | --- | --- |
| 1 | 文档通道**存在**，探针不必新增旗标 | `core/src/main.cpp:36` include `profile/glkv3_document.hpp`；`:210` 处理 `--ghostlock-app-call` |
| 2 | 四个具名状态**已存在** | `Status::NotInitialized` · `WriteOutcome::{NotOpen, NoOldBlock, NotSent}` |
| 3 | 选择的**位置**在根侧 | `profile/glkv3_document.hpp:108` 注明 root text key |
| 4 | 选择的**形状**是 `backend.<id>`（id 在段名里） | `main.cpp:95` 与 `:105` 的 `snprintf(..., backend.%s.execution.…, backend)` |
| 5 | 帧形状**有明文定义**（四字节大端长度 + 文档 + 可选会话帧） | `main.cpp:4-5` 文件头 |
| 6 | 现有读取器**不可复用**（匿名命名空间，且不读帧），但**模式可照抄** | `main.cpp:43-60`，共 12 行 |

**一处登记（不改，属另一批）**：`glkv3_document.hpp:108` 的注释提到 selection **tokens**，而 AGENTS 记载 token 形态已在 M5 删除（`e59a8479`）⇒ 该注释**可能过时**，留待读取层批次处理。

## 8 B 的落地清单（裁定到位即可开工，无需再勘察）

**新件**：

- `core/src/backend/cve_2026_43284/session_frame.hpp`（照旧 95 行的公开面：7 个常量 + `SessionFrameStatus` + 编码与读取两个函数）
- `core/src/backend/cve_2026_43284/session_frame.cpp`（照旧 174 行；把 `IpsecSaParams` 换成新树已有的 `SaMaterial`，把 `zeroize` 用文件内同语义替代并按规矩登记 TWIN）

**两处替换点**（实测已定位）：`encode_session_secret_frame` 的形参类型；`kEspIcvBytes` 常量（新树里对应的常量名以实测为准：`access_params.hpp` 的 `icv_len` 字段与 `kEspIcvMaxBytes` 一类，落时先量再写）。

**测试判据**（`core/src/tests/session_frame_test.cpp`）：

1. 编码出的载荷是**定长 84 字节**，且**不含**长度前缀；
2. 版本、kind、trailer（`0x34383238`）三处校验各自能拒（各造一个错）；
3. 读取侧：四字节大端前缀 + 长度上限 4096 的拒绝；
4. 往返：编码后再读，字段逐字节相等；
5. **证伪一次**：把 trailer 的期望值改一位 ⇒ 对应断言必须红、退出 1、零残留。

**接线四处**（与既有件同法）：文件清单 · `EVIDENCE_BINS`（整行做锚，短片段会静默不匹配）· rule（2 TAB）· check 行（1 TAB）；每个 replace 改完用 grep -c 核验。

**不属于本清单**（留给真机阶段）：探针组合文件与它的清理登记。

§9（命令级落地清单）已拆出：见同目录 `device-probe-43284-checklist.md`。
