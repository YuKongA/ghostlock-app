# 原生核心

[English](README.md)

GhostLock 将每个受支持的 frontend、backend 和 middleware 组合编译为 `Pipeline<Frontend, Backend, Middleware>`。运行时由解析后的 profile 选择组合。

## 组件

| 组件 | 可用实现 | 不可用的占位实现 |
| --- | --- | --- |
| Frontend：启动与 handoff | `root_child` | `umh_forward` |
| Backend：漏洞原语与写入阶段 | `cve_2026_43499` | `cve_2026_64560` |
| Middleware：一次写入的 route | `select_stack`、`tcp_zerocopy`、`multicast_waiter` | |

| 文件 | 职责 |
| --- | --- |
| [component_catalog.hpp](route/component_catalog.hpp) | ID、可用性、受支持的组合和分派目标。它是分派的唯一权威。 |
| [frontend_contract.hpp](route/frontend_contract.hpp) | Frontend 身份与执行契约。 |
| [backend_policy.hpp](route/backend_policy.hpp)、[backend_contract.hpp](route/backend_contract.hpp) | Backend 身份、注册表和 `BackendExecution<B, M>` 契约。 |
| [route_policy.hpp](route/route_policy.hpp) | `MiddlewarePolicy`、能力与 `w2_fast_repair_*` 等静态 hook。Android hook 的实现位于各 middleware 的 route 单元。 |
| [pipeline.hpp](route/pipeline.hpp) | 执行入口。在编译期检查组合与契约，返回 `RunResult`。 |
| [orchestrator.hpp](route/orchestrator.hpp) | `run_orchestrated_pipeline` 将每个已登记目标分派到经过检查的 pipeline。 |
| `route/*_route.{h,cpp}` | Route 实现与 Android hook 实现。 |
| `session/backend/cve_2026_43499_backend.*` | 模板化在 middleware 上的 W1/W2/W3 步骤，为每个已登记 route 实例化。 |

## 目录

| 目录 | 职责 |
| --- | --- |
| `attack/` | Profile 安装与共享攻击辅助函数。 |
| `race/` | PI 竞争的 owner，以及 waiter、owner 和 consumer 线程。 |
| `memory/` | 地址解析、堆与页状态、payload 编码。 |
| `session/` | 单次调用状态、运行配置、frontend handoff、阶段类型、探针与 victim 上下文。[exploit_session.hpp](session/exploit_session.hpp) 记录每个字段的所有者、借用者和终结点。 |
| `profile/` | GLK1 v2 编解码器、类型化模型、访问器与入口。 |
| `kernel/` | 目标 ABI、常量与偏移表。 |
| `support/` | Result 与 RAII 辅助工具、时间和致命错误。 |
| `kernelsnitch/` | 内核地址发现。 |
| `tests/` | 主机固定向量、生命周期、契约和 C/C++ 链接测试。测试探针不进入生产二进制。 |

## 构建边界与检查

Catalog、契约、policy 和 pipeline 头文件可在主机编译。Android 执行代码放在 route/backend 单元中，使用 `__ANDROID__` 条件编译，或仅纳入 Android 构建。Include 路径相对 `src/core`，如 `"route/tcp_zerocopy_route.h"`。

```sh
make -C src native-host-tests
make -C src ghostlock
make -C src lint-tidy
```

NDK 构建产出 `build/native/ghostlock`，必须零告警。`lint-tidy` 必须报告零 findings。攻击路径改动还需对比攻击函数，并执行[真机门禁](../../docs/development/engineering-standards_ZH.md)：

```sh
python3 tools/cmp_disasm.py <baseline> build/native/ghostlock
```
