# 重写所需的导出面（native 侧清单）

- 来源：**kotlin-app 只读勘察**（2026-10-09，按 ADR-0013 降级对拍后提交）· 由 Lead 落盘存档
- 权威：本清单**不是**契约，是**排期输入**；每条的实际权威仍在各自源文件

## 1 现存且可用（重写直接消费）

| # | 面 | 权威 → 导出 | 重写用途 |
|---|---|---|---|
| 1 | **字段表** | `profile/key_catalog.hpp` → `make -C src profile-manifest-v3`（+`-check`） | 类型映射与校验（读写两侧） |
| 2 | **链/组合目录** | `contract/chain_catalog.hpp` → `combination-manifest`（+pin） | **选择面与派生规则**（token/backend/route/path/steps/terminal/available/doc） |
| 3 | **stepset→步骤序列** | `contract/step_catalog.hpp` → `stepset-steps-manifest(-regen)`（+pin） | **种子数据**（按 ADR-0011 它**不是**顺序权威，是模版输入） |
| 5 | **LKM KMI 目录** | `lkm::kSupportedKmis` → `lkm-kmi-manifest` | 设备侧按 `uname -r` 派生 label 取件（payload 选择面） |
| 6 | **payload 契约** | `profile/payload_keys.hpp`（四键/三 kind/名字规则）+ 文档读取面（`schema`/`combination`/`sections`） | 发射器按它生成 |
| 7 | **脚本面 + op 注册** | `contract/script_surface.hpp`（7 条 + `ArgSpec`/`holds_resource`）· `contract/op_registry.hpp` | **Lua 计划模版生成必须对着这两份**（ADR-0011 的 gate 边界就在这） |
| 8 | **route 目录** | `profile/route_catalog.hpp`（token + wire） | **目前唯一带 wire 的目录** |

## 2 缺口（重写前需裁）

| # | 缺口 | 现状 | 据 ADR-0013 的处置 |
|---|---|---|---|
| 4a | **`wire` 列无权威** | `vocabulary-manifest.tsv` 期望每行有 `wire`，但只有 **route** 有权威（`RouteCatalogEntry{token, wire}`）；`kComponents` 只有 `{kind, token, selectable}` ⇒ 该列**既生成不了也核对不了** | **只保留有权威的部分**（route），其余三类以**具名缺口**替代；并恢复导出器（本会话第四个同类孤儿） |
| 4b | **`combination_resolve` 不存在** | `combination-resolve-vectors.tsv` 的 `expected_token` 声明由该函数计算，而全仓**零命中** | 该文件**冻结**（保留为历史，标注权威已亡）；重写时由**新 resolver** 的设计重新定义向量 |
| 9 | **设备事实/几何无导出面** | `memory/target.h`、KMI 目录等为 C++ 常量 | 若要「无手抄」⇒ 需**导出面**（目前无）；属重写设计 |

## 3 已落地且按 ADR-0013 视作【镜像/默认值面】的 Kotlin 改动

（**不扩展、不主动回退**；若要回退需**另行下令** —— 那属新一轮 Kotlin 工作）

- combination 根标量 + 三根键删除（encoder/adapter）· 4 个泄漏搜索键（资产 + 两份 manifest）· stepset/combination 副本对齐 pin · 枚举常量与 `StepSetKind` token 改名 · ViewModel 步骤判断改 `seccomp_bypass`。
