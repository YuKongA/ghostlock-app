# DFRoot 路径实现完整度审查

- 日期：2026-10-19
- 依据：`https://github.com/diabl0w/DFRoot`（v4.6，commit c274137）
- 约束：先完成代码与审查，不做设备探测；真机测试放到最后

## 1. DFRoot v4.6 实际流程

```text
1. IpSecManager 建 AES-CBC、无认证 SA
2. patch_helper(): crash_dump64 offset 0 写 splicehelper
3. patch_ko(): 经 crash_dump 桥把 KO 写进 vendor carrier
4. patch_hook(): libc++ sentry 写 464B libcxx + trampoline
5. createOrphanProcess(): 双 fork 孤儿，等 init 收尸触发
6. cleanup(): 先恢复 trampoline，再恢复 shellcode
```

## 2. 已实现的 Kotlin 直写路径

| DFRoot 步骤 | 本实现 |
|---|---|
| SA 建链 | `ensureRawSaIfRequested`：AES-CBC、无认证、独立 SPI/端口 |
| 页缓存写 | `native_raw_esp.cpp` + `RawEspWriter`：vmsplice + splice |
| payload | `DfrootLibcxx`：468B，实际写 464B |
| first-inst | `0x1C8` 写站点原指令 |
| jumpback | `0x1CC` 写回跳分支 |
| site | `0x6eb54` 写 `b landing+0x90` |
| 触发 | `hook.trigger` 双 fork 孤儿 |
| 恢复 | 先恢复 site 块，再恢复 464B shellcode |

## 3. 关键提交对应的实现

| 提交 | DFRoot 修复 | 本实现 |
|---|---|---|
| f7068d9 | jmpback/first-inst 必须写进实际 `buf` 副本 | Kotlin 直接 patch `DfrootLibcxx.bytes()` 副本 |
| 473b318 | 先恢复 trampoline，再恢复 shellcode | `dfroot restored site=... stage=...` 顺序一致 |
| e47ea6e | libc++ 壳 clone worker + setcon insmod | payload 原样嵌入 |
| d3cd07a | BTI/PAC LKM 兼容 | 属 LKM 侧，本实现未改 |
| 0fed9a8 | SELinux avc_has_perm patch | 属 LKM 侧，本实现未改 |

## 4. 有意的差异

1. **未写 crash_dump64 splicehelper**
   DFRoot 用它桥接 vendor 读写；本实现的模块写入走现有 native `Landing43284` 的 crash_dump 远端桥，`module.writeImage` 已把 769 块写进 vendor carrier，root shell 探针 insmod 成功并输出 `UMH_OK`。因此 helper 不是本实现的必需步骤。
2. **恢复时机**
   DFRoot 在模块成功后立即恢复；本实现轮询 `/dev/df`，最多 5s，随后按 trampoline → shellcode 顺序恢复。
3. **入口开关**
   DFRoot 由 app 自动执行；本实现仍由 `/data/local/tmp/.gl_dfroot_payload` marker 控制，避免未验证路径进入默认生产。
4. **LKM**
   使用 GhostLock 自己的 LKM；DFRoot 的 BTI/PAC 与 AVC patch 未移植。

## 5. 仍未完成/未验证

- DFRoot 路径在模拟器上尚未完成一次“触发成功且系统稳定”的闭环；此前 site patch 后系统 offline。
- 未写 crash_dump64 helper；若后续确认模块写入不等价，再补 helper。
- 未做真机门禁；按用户指令，先完成代码审查再上真机。

## 6. 审查结论

- Kotlin 直写、payload 布局、first-inst/jumpback、site 分支、恢复顺序均已有代码；
- 与 DFRoot 的已知关键修复（f7068d9、473b318）已对齐；
- 剩余风险集中在 site patch 后系统稳定性与 helper/KO 等价性；
- 不建议在完成上述两项审查前做真机门禁。
## 8. 静态 crash_dump64 入口：当前权威结论（2026-10-19）

- `EntryPayload.plan(kotlinTarget)` 现在经 `KotlinElfPlanner.planEntry` 派生 site/landing/vaddr/payloadMax/displaced；不再用固定 0x428/0x430/3744。
- 静态 payload 的 branch 目标是 landing 本身；`EntryPayload` 固定常量只保留为测试夹具断言。
- APEX 目标的 raw ESP 写入/系统稳定性仍只有实验记录，未达到生产批准；静态入口路径继续由实验 marker 控制。
- 本文件与 `43284-kotlin-dynamic-hook-plan.md` 中较早的 0x430 页对齐/固定 plan 段落视为历史记录，以本节 + design-critic 回应为准。

## 7. 代码完整度结论（2026-10-19）

- Kotlin 直写、DFRoot payload、first-inst/jumpback、site/trampoline、触发、恢复顺序均已实现；
- `crash_dump64` helper 的缺口由现有 native `Landing43284` 远端桥覆盖，模块写入已实测 769 块并可在 root shell 下 insmod；
- AttackFlowGuard 静态守卫通过：攻击流程未重新引入旧 signal marker；
- 新增 `DfrootStageTest`：锁定 464B stage、0x1C8 first-inst、0x1CC jumpback、branch 0x1400ADA7；
- `:app:testDebugUnitTest` 153 tests PASS，`:app:assembleDebug` PASS；
- 尚未做设备门禁；DFRoot 路径仍由 marker 控制，未切默认生产。


