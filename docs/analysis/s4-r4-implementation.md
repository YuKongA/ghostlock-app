# S4 · R4 实施设计：43284 policy 完整表达（string 路径 + 握手参数入文档）

> 上游：`config-wire-redesign-plan.md`（R4、§4.2、§5.3）。**顺序：本批先于 R2b**（CLI 最小化），取证见 `branch-plan.md`。
> 前置：R1 / R2 / R2c / R2c-2 均已完成。

## 1. 目标

1. **接通 `WireKind::String`**（R1 只预留词汇，绑定/视图路径尚未承载字符串）；
2. **43284 策略字符串化**：`carrier_path` / `lkm_path` / `defex_symbol` 由 uint 选择器 token 改为 string 路径，删除 token 表与 token→路径 迂回；
3. **握手/等待参数入文档**：把现硬编码的 `wait_timeout_ms=15000`、`module_poll_*` 搬进 `backend.cve_2026_43284.*`（默认值 + `default_used` 诊断）；
4. Kotlin/UI：高级设置可填并校验路径与参数；manifest 由 uint 改 string 并重生成。

## 2. 逐文件清单

**native：string 端到端**
- `src/core/profile/schema.hpp`：`wire = String` 的绑定语义 —— 视图字段为 `std::string_view`（指向解码文档缓冲，GLKv3 已强制 ≤256 B）；
  `materialize_with_defaults` 需能承载字符串（不再走 u64 通道）；
- `src/core/tests/`：新增「string 绑定 / 超限拒绝 / 缺省与默认」用例；manifest 导出 `string` 行。

**native：43284**
- `backend/cve_2026_43284/schema.hpp`：三字段改 `wire = String`（默认：carrier 取设备首个存在候选；lkm 取 `$GHOSTLOCK_HOME/helper.ko`；defex 见现状）；新增握手参数字段与默认值；
- `lkm/lkm_policy.{hpp,cpp}`：删除 token 解析，直接消费路径字符串（默认解析结果必须与现状等价）；
- `lkm/lkm_image.cpp` / `backend_terminal.cpp`：消费 string 路径；硬编码 15000/poll 常量改为读文档（缺省走 schema 默认 + `default_used`）。

**Kotlin / 资源**
- `profile-core` 的 `NativeProfileGlkv3Adapter`：按 manifest 发 string（不再编 token）；
- 高级设置 UI：路径与握手参数可编辑 + 校验（≤256 B、绝对路径提示）；
- 两份 `profile-manifest-v3.tsv`（app test 与 profile-core main 资源）重生成，对拍测试同步。

## 3. 不变量

- `schema == 3`、canonical 不变、string ≤256 B 超限 fail-closed；
- **默认路径下 43284 取值不变**：`profile_resolved` 与 R1/R2 一致（`lkm_path` 变为路径字符串而非 token）；
- 43499 与 HOCON 布局不动；CLI 标志本批不删（R2b）；无新增可变全局；零告警；防火墙不变。

## 4. 门禁

| 门槛 | 判据 |
|---|---|
| host / NDK / lint | 全绿（含新 string 用例） |
| Kotlin | `:profile-core:test` + `:app:testDebugUnitTest` EXIT=0（manifest 对拍含 string 行） |
| 真机 43284（默认） | app-call `EXIT=0`；`profile_resolved` 与 R1/R2 一致 |
| 真机 43284（自定义路径） | 文档里把 `lkm_path` 指向另一份 helper 副本（如 `/data/local/tmp/helper_custom.ko`）→ 该副本被加载（证明策略由文档承载） |
| 真机 43499 | 冷启回归 PASS |

## 5. 风险

- string 绑定是新数据结构路径：`std::string_view` 指向的缓冲必须在绑定期间存活，绑定不得复制或悬垂；
- token 改 string 会改变 wire 值（uint → str）→ Kotlin 与 native 必须同批，manifest 三端对拍会抓；
- 硬编码参数入文档后默认值写错会改变运行时间 → 默认值必须与现状逐项一致，并用 `default_used` 对照。
