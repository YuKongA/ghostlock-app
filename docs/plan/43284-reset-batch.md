# 43284 重置批次计划

- 日期：2026-10-19
- 设计：docs/analysis/43284-refactor-design-v4.md
- 上游：DFRoot@c274137
- 取代：DFRP P1/P2/P3 旧会话实现；KPLAN 用户裁定不变（Kotlin plan/write，C++ 仅 syscall），v4 只细化阶段
- 状态：B0–B6 完成；B7-A 基线 7/7 Stage OK；B8/B9 GhostLock 特有功能未做

## 批次

| 批 | 内容 | 验收 |
|---|---|---|
| B0 | 删除 43284 后端/JNI 具体实现，保留 POD/pure raw_esp | 代码删除已完成；B1 前重跑并归档单次 XML/CTest，未在本计划宣称全绿 |
| B1 | C++ gl_esp_* syscall + include_firewall_test | 已完成：nativeBuildHost、ctest 74/74、firewall 0 命中 |
| B2 | C++ gl_helper_* + gl_fadvise_drop | 已完成：nativeBuildHost/Android、ctest 74/74 |
| B3 | JNI 薄导出 NativeRawEsp/NativeHelper | 已完成：nativeBuildAndroid、7 个 JNI 符号核对 |
| B4 | Kotlin Stage/BlockWriter | 已完成框架：BlockWriter/BlockAccess、StageRunner、Environment/Helper/Module/Hook/Trigger/Wait/Cleanup + HookStage fake 测试 |
| B5 | 删 43284 专用：params_43284/factory 分支/契约 AccessParams43284/Native43284/RawEspWriter/KotlinPageCacheWriter/Assembled43284/TypedBackendProbe；保留 interface.hpp 的 AccessParamsPod、43499 Session/NativeProtocol | 已完成：旧链/旧 JNI/旧测试删除，默认只走 Stage |
| B6 | 全量 include 防火墙 + marker 扫描（rg 命令见 v4 §7） | 已完成：include_firewall 0 命中，marker rg 0 命中 |
| B7-A | DFRoot 主链设备基准（helper→KO→libcxx→trigger→cleanup） | 7/7 Stage OK、模块已加载；仅证明 A 基准，不含 GhostLock 特有功能 |

| B8 | GhostLock 特有功能：PayloadScriptSpec/custom payload renderer、manager/libksud 选择、bootstrap 环境、失败/回滚 | 待做；当前 PayloadScript 固定脚本、root 写 payload.status//dev/dfm6，与 payload-finish 白名单冲突，B8 必须替换 |
| B9 | LKM AVC/Oppo/Defex/BTI、restore_enforce/soft reboot、最终 GhostLock 门禁 | 待做 |

## 当前已知

- 43284 backend 实现已删除；interface.hpp 保留 AccessParamsPod，access_params.hpp B5 删除。
- ko_target 注入槽已修正为 0x47/64B；0x88 是偏移字。
- 当前 PayloadScript 是固定占位：路径 files/payload，写 payload.status、touch /dev/dfm6；B8 要换成 app 白名单内的自定义脚本，root 只读执行。
- 未做设备测试。
