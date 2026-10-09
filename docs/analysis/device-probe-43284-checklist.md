# 附件：真机探针的命令级落地清单

> 本件是 `device-probe-43284-plan.md` 的附件 · 2026-10-09

## 9 命令级落地清单（裁定一到，第一轮即可落盘并跑门禁）

**新建两个文件**：

- `core/src/pipeline/probe_43284.cpp`：读取（四字节大端长度 + 文档 + 可选会话帧，模式照 `main.cpp:43-60` 的十二行并补读帧）；把帧字节填进 `SaMaterial`；经工厂取 `Access`；`initialize`（材料 + 位置）；一次 `write`；`shutdown`。**不打印任何密钥**。
- `core/src/tests/probe_43284_test.cpp`：用假的门与假的 splice 覆盖四条失败路径各一次（未初始化 / 门未开 / 无旧块 / 未发送），每条必须打出对应的具名诊断；并证伪一次。

**四处接线锚点（当前实测，落时以当时为准）**：

| 位置 | 当前行 | 做法 |
| --- | --- | --- |
| 文件清单 | 第 168 行之后（`src/tests/real_splice_io_test.cpp` 那一行） | 追加三行 |
| `EVIDENCE_BINS` | 第 274 行（一整行长行） | **整行做锚**追加，短片段会静默不匹配 |
| rule | 第 575 行那条之前 | 三行，2 TAB 缩进 |
| check 行 | 第 793 行那条 echo 之前 | 两行，1 TAB 缩进 |

Makefile 当前 1215 行，可作为落盘前后的一致性核对。

**验收命令与期望**：`make -C core host` 与 `make -C core check` 均退出 0；check 日志里出现新增的一行 check 与其 PASS 行；`make -C core dangling-check` 的 missing 为 0 且总数增加 2。

**真机阶段**（设备侧，仍需会话给的 SA 材料）：跑一次探针，按 §4 取证据并归档，随后按 §5 的清理表逐条删除并核验。

## 测试探针的一条硬约束（写测试前先看，可省一轮返工）

`probe_43284_write_once` 从两个不同的接口读同一个流：文档走 `std::fread(stdin)`，会话帧走 `read_session_secret_frame(0, …)`，即文件描述符 0。因此测试不能只准备一个 `std::string` 或 `std::FILE`，它必须让**文件描述符 0** 指向一份可读的字节流 —— 做法是用 `pipe` 造一对描述符、把读端 `dup2` 到 0、写完关闭写端，然后调用。

另外两点也影响判据的写法：

1. 帧在文档**之后**读，顺序不能反；测试若先放帧的字节，探针会把它们当作文档前缀吃掉；
2. 帧缺席时读侧会 `poll` 到超时再返回 `Missing`，所以「无帧」那一组用例会**花掉一次超时**，测试应当预期它耗时而不是判它挂住。

## 设备侧执行步骤（照抄即可）

前置：设备已连、构建产物已就位；SA 材料来自会话，不落盘。

1. **构建**：`make -C core ghostlock`（NDK），产物 `build/native/ghostlock`。
2. **准备输入流**：把一份长度前缀的 GLKv3 文档写进文件，其后**紧接**该会话的帧字节（84 字节，无长度前缀）。顺序不能反：文档在前，帧在后。
3. **推送并运行**：把产物与输入流推到设备，用同一命令行喂入（`--ghostlock-app-call` 是既有入口，不需要任何新旗标）。
4. **期望输出三行之一**：
   - `probe-43284: no session frame, so this is not a 43284 run` —— 帧缺席，说明这次不是该后端的运行，**这是正常结果**，不表示失败；
   - `probe-43284: the session frame was refused as N` —— 帧被拒，N 是具名状态号，按号定位；
   - `probe-43284: write status N, value V, shutdown ok|refused` —— 走到写。status 0 与 value 16 为成功。

5. **取证据**：设备 `Download/ghostlock-debug-log/<时间>/` 下的 `*.log.txt`、`profile.conf`、`profile.bin`，加本次运行的控制台输出。
6. **归档**：按 `docs/analysis/device-gates/` 的格式写一份门禁记录，含 uname、页大小、时间、构建标识与上面三行之一的原文。
7. **清理（归档后立即执行）**：删除 `core/src/pipeline/probe_43284.cpp` 与 `core/src/tests/probe_43284_test.cpp`，从 Makefile 撤掉清单行、`EVIDENCE_BINS` 项、rule 与 check 行；然后 `grep -rn probe core/src core/Makefile` 应为 0 命中，`make -C core host && make -C core check` 应为 0。
8. **记录**：提交信息写明清理完成，并在 `pending-rulings.md` 里把相关条目划掉。

**诚实说明**：以上步骤在本会话内无法执行 —— 需要设备，且 SA 材料只能来自会话。本会话能完成的是把它准备到照抄即可的程度。

## 清理表（补：入口驱动的临时件，两后端共用）

第二支临时件，与上一节那支探针**不同**：它**不是**旁路诊断，而是**走生产路径**的驱动 —— 读文档、组 `PlanInput`、过 `plan_gate`、`CompositionRoot` 绑执行器、跑 `run()`。

| 临时件 | 位置 | 删除条件 |
| --- | --- | --- |
| 入口驱动函数 | `core/src/main.cpp`（一处调用 + 一处声明） | 真机结果归档后立即删 |
| 若需驱动专用 TU | `core/src/pipeline/`（仅当 `main.cpp` 放不下时） | 同上 |
| Makefile 接线 | 清单 / `EVIDENCE_BINS` / rule / check 行（各一处，按落时行号） | 同上 |

**核验**：删后 `grep -rn 'probe-43284\|drive_' core/src core/Makefile` 为 0 命中；`make -C core host && make -C core check` 为 0；`markdownlint` 为 0。

**对齐**：本表把上一支探针（已删 ✓）与这一支驱动**并列登记** —— 文件名里的 43284 是沿革，覆盖范围是**两个后端**。

## 到 root 的真实缺口（实测）

缺口（2026-10-09 实测修正，先前那句“全是装配”过于乐观）：

1. 43499 的决策与链 —— 完成（request_for · RaceAction）.
2. 生产侧执行器 —— 完成（三条路径各一个类型，编译期 static_assert）.
3. 传递 —— 架子完备（plan_gate，十二种以上具名理由，含 executor_available）.
4. 真缺件：发现与输入的发生者。memory 一族（write_layout · write_encoder · credential_image）与 leak 一族（snitch_run · leak_search · discovery_result）在树里且各有测试，但 write_layout 的调用点只有测试、LayoutInputs 只有一个测试构造点，四个地址（page_value · credential_value · default_fops · credential_fops）没有任何生产构造点。库在，驱动不在。
5. 入口驱动，临时件，清理已登记于 checklist.

## 清理表（补：临时入口的两个开发输入）

与上一节同一个临时入口（`pipeline/run_pipeline_temp.{hpp,cpp}`）同批登记，两项**开发输入**：

| 临时物 | 位置 | 删除条件 |
| --- | --- | --- |
| 开发载体路径 `kDevCarrier` | `core/src/pipeline/run_pipeline_temp.cpp`（一处常量） | 与入口同批删；正式路径里载体由选择/资源给定，见 `AccessCarrier` |
| 开发 payload 路径（若尾步骤用到） | 同上 | 同上 |

**核验**：删后 `grep -rn 'run_pipeline_temp\|kDevCarrier' core/src core/Makefile` 为 0 命中；`make -C core host && make -C core check` 为 0。

**为何允许**：`--allow-dev-target` 的注释写的就是这个用途（只放宽绑定期 carrier 校验），所以这两个输入走的是既有门，不是新旁路。

| 开发布局偏移 `kDevLockOffset` 等五个常量 | `core/src/pipeline/run_pipeline_temp.cpp`（一处声明） | 与入口同批删；正式路径按 manifest 从 `backend.cve_2026_43499.abi.payload` 段读 |

**为何不是回退**：目标 profile 文件明文禁止 no default fallback，所以这五个数不是「缺键时补一个」，而是**声明式临时源**；入口在用它之前会打印一行说明它不来自文档。

**真机结果**：见 docs/analysis/device-gates/20261009-1621-temp-entry-steps.md（证据归门禁归档，本表只留登记）。
**真机结果**：见 docs/analysis/device-gates/20261009-1621-temp-entry-steps.md（证据归门禁归档，本表只留登记）。
**构建**：须覆盖 NDK_TOOLCHAIN 到 NDK 30（原因与命令见门禁归档）。
| 桩桶 StubBuckets | run_pipeline_temp.cpp | 与入口同批删；正式用真实规则 |
| 表尺寸钉值 256 | run_pipeline_temp.cpp | 与入口同批删；正式按 profile 与核数 |
