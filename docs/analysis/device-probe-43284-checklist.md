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
