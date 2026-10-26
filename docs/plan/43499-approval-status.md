# 43499 批准状态（单表 · 唯一权威）

- 日期：2026-10-21
- **本表是 43499 批准状态的唯一权威**。其他文档（kt-step 计划、native actor/child 计划、route 计划、MASTER-PLAN §13）
  只允许引用本表，**不得再各自声明"已批准/待批准"**。发现不一致 ⇒ 改那份文档，不动本表。
- 状态只有三种：**已批准**（用户明确同意，可进生产或可执行该动作）、**待批准**（提案，未生效）、**不做**（明确排除）。
- 「原则允许」与「生产启用」分开记：前者是方向，后者才是可写生产代码的依据（2026-10-20 记录的真实形态）。

## 1. 基线裁定

| 裁定 | 内容 | 状态 | 依据 |
| --- | --- | --- | --- |
| 基线 | **所有逻辑在 Kotlin；C++ 只保留最薄 JNI syscall 接口** | 已批准 | 用户 2026-10-20；[43499-kt-step-plan.md](43499-kt-step-plan.md) 头部 |
| 层级拆分 | 第 1 层＝逐 syscall JNI；第 2 层＝actor batch / victim child helper | **原则＝已批准；生产启用＝仅第 1 层**（第 2 层生产启用＝待批准） | [43499-kt-step-plan.md](43499-kt-step-plan.md) §0 |
| PI 粒度 | 每个 futex 一次 JNI 可能破坏 PI 窗口时序 ⇒ 不能直接作生产方案 | 已批准（作为约束） | `docs/analysis/jni-six-targets-params-plan.md:58-65`；`pi-window-constraints.md` |
| JNI v4 基线取代 | Kotlin/Lua拥有全部业务编排；Native可持有物理资源并完成最小不可拆transaction | **已批准** | 用户 2026-10-23 指令“按照计划”；K1 历史薄层保留 |

## 2. 批次状态

| 项 | 内容 | 状态 | 实现状态 | 依据 / 备注 |
| --- | --- | --- | --- | --- |
| **K0a** | legacy 隔离：旧 43499 库不进 APK、移 `KernelMemoryEngine.production()` | **已批准** | **已实现**（`unzip -l` 只见 `libgl43499syscalls.so`） | kt-step §5；属第 1 层执行 |
| **K1** | Kotlin `KernelSyscalls` + C++ 逐 syscall 薄库 `gl43499syscalls` | **已批准** | **已实现**（`nativeTestHost` 76/76、Kotlin 3/3、零告警） | kt-step §0/§5/§11 |
| **K2** | Kotlin 页/凭据/布局 + `KtStepEngine` 骨架 | **已批准** | **已实现**（布局 7 + 引擎 9） | 同上；只依赖第 1 层 |
| **K0b** | 最小 actor-batch PI 探针（一次 attempt，**非生产 route**） | **已批准**（探针用途） | **已跑**：AVD `requeue=1`/`wait=0`；**不标 supported** | kt-step §4；[k0b-probe-20261020-0952-pass.md](../analysis/device-gates/k0b-probe-20261020-0952-pass.md) |
| **K3** | 生产 route + 页写入（Kotlin 侧决策 + C++ actor batch） | **待批准** | Kotlin 离线 K3.1–K3.7 已移植（70/70）；**生产 route/page = 未通** | [43499-kt-step-plan.md](43499-kt-step-plan.md) §0/§15–§16；MASTER-PLAN §13「K3 生产阻断」 |
| **K4** | Kotlin prepare/discover/victim 编排 +（可选）C++ child helper | **待批准** | K4a 已备：`ProcessVictim`/`ProcessVictimSession` lifecycle（host + AVD） | kt-step §5 K4；[43499-production-victim-plan.md](43499-production-victim-plan.md) |
| **K5** | 端到端 `victim uid=0` + 收尾 | **待批准** | 未开始 | kt-step §5；未过真机门禁不得标 supported |

## 3. 跨层提案（第 2 层）

| 项 | 内容 | 状态 | 证据 / 边界 |
| --- | --- | --- | --- |
| **actor batch（原则）** | 允许 native 建 waiter/owner/requeue 三线程、一次 JNI 执行固定 syscall 顺序 | **已批准**（原则） | 用户 2026-10-20 记录：[43499-native-actor-child-plan.md](43499-native-actor-child-plan.md) §13 |
| **actor batch（生产启用）** | 让生产 route 依赖 actor batch 写内核页 | **待批准** | kt-step §0「原则＝已批准，生产启用＝待批准」；MASTER-PLAN §13「K3 生产阻断」 |
| **C++ child helper（原则）** | 允许 C++ `posix_spawn` 子进程承载提权后命令 | **已批准**（原则） | 同上 §13 |
| **C++ child helper（落地）** | 43499 生产链调用 native child JNI | **不做** | 已被 [43499-production-victim-plan.md](43499-production-victim-plan.md) 取代：victim 用 Kotlin `ProcessBuilder`（`/system/bin/sh` + `id -u`）；`NativeAttemptJni` 的 child 入口保持 `ENOSYS` |
| **Kotlin 线程/亲和性** | PI 外 `sched_setaffinity`；PI 内零分配/零打印/零间接分派 | **已批准** | kt-step §3.1；**PI 内 `sched_setattr` 仍禁**——消费者 `setattr` 的 PI 例外只按 §4 的 route 记录，不推广 |

## 4. route 状态

| route | 方向 | 状态 | 实现 / 门禁 |
| --- | --- | --- | --- |
| `multicast_waiter` | PI-futex 三演员 + stamp/page tuple | **已批准**（含 **PI 例外**） | [43499-route-port-plan.md](43499-route-port-plan.md) §7a：用户 2026-10-20 接受 PI panic 为概率性、非破坏事件（后果＝重启），明确批准进入该 route；AVD route gate PASS（`cve43499-route-avd-20261020-1619.md`） |
| `select_stack` | 独立 JNI 家族 + 独立 TU | **已批准**（方向与 PI 例外） | 计划整体仍被 design-critic 阻断（[43499-three-routes-production-plan.md](43499-three-routes-production-plan.md)）；AVD success gate：`cve43499-select-success-avd-20261020-1910.md`（`statusStep=0`、`successes=1`） |
| `tcp_zerocopy` | 同上 | **待批准** | 无 route 实现，仅 legacy snapshot 端口 + `arm_route()==false`；[43499-tcp-native-port-plan.md](43499-tcp-native-port-plan.md) 为 planning only；AVD `step=40`（零 FakePage）不算通过 |
| 通用 `routeId` 参数 + 单一 JNI | — | **不做** | [43499-ktstep-architecture-plan.md](43499-ktstep-architecture-plan.md) §1：每条 route 自己的 JNI 家族与 TU |
| 三 route 合并成一个参数化 route | — | **不做** | 同上 |

### 4.1 JNI v4 同批取代候选（未批准不生效）

| 项 | 候选内容 | 状态 | 生效代价/偿还 |
|---|---|---|---|
| route 所在层 | 取代“每 route JNI/TU”：Native 只给三个命名 mechanism entry；Kotlin/Lua 组合 route | **已批准** | 用户 2026-10-23 指令“按照计划”；typed request + 三路真机偿还 |
| actor batch 生产 | 最小不可拆 PI actor transaction 在一次 Native call 内完成 | **已批准** | 用户 2026-10-23 指令“按照计划”；窗口调用图/真机门禁 |
| typed handle | 值调用无状态；Page/Actor/Discovery/Victim 等物理资源用 handle | **已批准** | 用户 2026-10-23 指令“按照计划”；并发矩阵和残留检查 |
| registry 例外 | 每 DSO 一个固定容量可写 registry，PI 窗口外访问 | **已批准** | 用户 2026-10-23 指令“按照计划”；BSS manifest/fork gate |
| victim/Finish | protocol child 用 Native handle；普通 victim 仍 ProcessBuilder；UI脚本由 rooted child 中性触发 | **已批准** | 用户 2026-10-23 指令“按照计划”；pid/hash/marker/SELinux 真机门禁 |
| C11 Page/Actor header-only 契约 | Placement→Page 所有权转移；Actor prepare 时绑定物理 Primitive；execute 无 tag；不接 CMake/JNI | **已批准（仅头文件）** | 用户 2026-10-26 明确要求从零写 `.h`；实现与生产接线仍按独立批次门禁；见 [43499-c11-header-contract-plan.md](43499-c11-header-contract-plan.md) |
| JNI transport | 具名 Kotlin DTO ↔ 显式 JNI 字段翻译 ↔ C11 九入口；当前 core 用 fail-closed unavailable 实现 | **已批准（仅 transport）** | 用户 2026-10-26 明确要求“修改jni”；不批准物理原语实现；见 [43499-jni-transport-plan.md](43499-jni-transport-plan.md) |

批准方式：用户 2026-10-23 指令“按照计划”编写架构 Kotlin class；第1层历史薄层继续保留，第2层最小 transaction 生产启用。2026-10-23 六项同步生效；2026-10-26 header-only 项独立生效。

## 5. 本计划范围内明确不做

| 项 | 状态 | 依据 |
| --- | --- | --- |
| UI / 配置 / profile 接线 | **不做**（本阶段） | kt-step 头部与 §9 |
| 43284 / 64560 | **不做** | kt-step §9 |
| CLI / 临时旁路 | **不做** | MASTER-PLAN §0.1；kt-step §9 |
| Lua 自由跨 backend 编排 | **待批准（JNI v4）** | UI run 仍经 FlowLuaImpl；脚本内部可直调 scope-bound API |
| 删除 legacy C++ 43499 | **不做**（隔离保留，`git show 92d55d3e:<path>` 恢复清单） | kt-step §7 CBN / §9 |
| 把 fixture 当生产成功 | **不做**（禁止） | MASTER-PLAN §13；kt-step §16 |

## 6. 计划索引（谁在办、谁已降级）

| 计划 | 角色 | 状态 |
| --- | --- | --- |
| [43499-kt-step-plan.md](43499-kt-step-plan.md) | **唯一生产计划** | 在办（K1/K2 已实现，K3–K5 待批准） |
| [43499-43284-jni-rewrite-plan.md](43499-43284-jni-rewrite-plan.md) | JNI 重写（Kotlin-first） | **待批**（design-critic v1 不可进；v2 待复核）。批准后接管 kt-step 的生产计划地位；本表仍是批准状态唯一权威 |
| [43499-route-port-plan.md](43499-route-port-plan.md) | multicast 生产 route | 在办（PI 例外已批；页面落点未证） |
| [43499-three-routes-production-plan.md](43499-three-routes-production-plan.md) | 三 route 生产化 | 阻断（design-critic）；select 已 AVD pass、tcp 未通 |
| [43499-production-victim-plan.md](43499-production-victim-plan.md) | victim 生命周期（Kotlin） | 已实现（host + AVD lifecycle） |
| [43499-prepare-port-plan.md](43499-prepare-port-plan.md) | prepare 端口 | 已实现（AVD `status=0`/`shapingFd`），页落点未证 |
| [43499-tcp-native-port-plan.md](43499-tcp-native-port-plan.md) | tcp route | planning only |
| [43499-ktstep-architecture-plan.md](43499-ktstep-architecture-plan.md) | route JNI 家族与包结构 | 设计已批准；实现分批 |
| [43499-kotlin-package-refactor-plan.md](43499-kotlin-package-refactor-plan.md) | 包结构重构 | 设计已批准；部分落地 |
| [43499-native-actor-child-plan.md](43499-native-actor-child-plan.md) | actor batch / child helper 细节 | **legacy 提案**（生产启用待批准；见 §3） |
| [43499-production-engine-plan.md](43499-production-engine-plan.md) | K1 旧适配器 | **legacy**（不接入生产） |
| [43499-native-device-ops-plan.md](43499-native-device-ops-plan.md) | N1b/N2/N3 C++ 逻辑 | **legacy**（不接入生产） |

## 7. 维护

- 新增批准 ⇒ 只改本表一行（状态 + 依据日期 + 出处），并在对应计划里改为「见本表」。
- 结构规则（执行权威、close 互斥）见 [execution-architecture.md](execution-architecture.md)；进度见 `MASTER-PLAN.md`。
