# Debug Probe 后端（2026-11-02 用户要求）

> 状态：S/M 级实现说明（非攻击路径、非 L 级）；debug build 专用；release 行为不变。
> 用户原话：「新增一个debug probe后端，唯一的作用是打印所有环境信息到log，同时测试其他ui框架功能如log落盘」。

## 目标

- 新增一个**仅 debug 可选择**的 backend（`BackendDebugProbe`），无任何攻击/文件写/进程注入副作用；运行后把环境信息写入 Flow log，并验证 log 落盘与 UI 框架管线。
- 复用新进程通道：`BackendDebugProbe.flowSymbol` → `FlowLaunchParent.launchSymbol` → App `:flowhost`（后续也可 Shizuku）→ `FlowDebugProbeEntry.entry`。
- 打印内容（尽量全，纯只读）：
  - host 注入参数 `host.uid/pid/domain/run_dir/native_lib_dir/log_path`；
  - `Build.*`、`System.getProperties()`、环境变量、`/proc/version`、`/proc/self/status` 摘要、SELinux enforce 值、boot_id、uptime；
  - 固定探针行（`debug-probe:line:<n>`，≥128 行）用于验证 log fd 并发写入/落盘/UI 回调；
- log 落盘测试：androidTest 断言 `host_log_path` 文件存在、非空、包含探针标记；UI 输出回调收到同样行；evidence 记录 `debug_probe=ok`、`log_lines=<n>`、`host_log_path`。

## 注册与构建期隔离

- `Backends.builtins` 包含 `BackendDebugProbe`（受 `BuildConfig.DEBUG` 控制）。
- `BackendDebugProbe.selectable = BuildConfig.DEBUG`；`flowSymbol = if (BuildConfig.DEBUG) FlowSymbol("...FlowDebugProbeEntry","entry") else null`。
- `Backends.isSelectable` 在 debug 允许该 spec（release 不允许）；`FlowEntryRegistryAssembly.productionSymbols()` 在 release 自然排除（flowSymbol=null）。
- release 构建仍编译 entry 类但不注册/不可选；不改变任何生产 backend/registry 行为。

## 门禁

- JVM：`FlowEntryRegistryAssemblyTest`/`BackendFlowSelectionTest` 按 debug 期望更新（release 仍只有两个 Glued 生产入口 + 无 probe）。
- 设备（debug APK）：`DebugProbeHostSmokeTest` 经生产 dispatcher 执行 probe，断言 Finished.Ok/host pid≠app/输出含环境行/`host_log_path` 文件含 `debug-probe:done`；App 路径先行，Shizuku 可用时同流程补测。
- 不跑真实漏洞 ELF；不改 AIDL/manifest/native。

## 完成记录（2026-11-02）

- 实现：`backend/debugprobe/{BackendDebugProbe,FlowDebugProbeEntry}.kt`；debug-only `selectable/flowSymbol`，release NotReady/不可选/不在 registry。
- 环境打印：host.*（uid/pid/domain/run_dir/native_lib_dir/log_path）、Build 扩展、System properties/env（敏感键名脱敏）、`/proc/version`、`/proc/self/status`、`/proc/meminfo`（8 行）、`/proc/cpuinfo`（6 行）、`/proc/cmdline`、`/proc/self/attr/current`、SELinux enforce、boot_id、uptime、Locale/TimeZone、Runtime 内存、CPU online/present/availableProcessors、NetworkInterface、128 行 marker + done。
- log 落盘：run_dir 外持久化 `files/ghostlock-glue/flowhost/debug-probe/debug-probe-last.log`（0600；evidence `debug_probe.persist_path/persist_bytes`）；run_dir 内 `host_log_path` 在正常终态按设计被 cleanup。
- 门禁：全量 JVM **421 tests/0/0/0**；debug/androidTest/release 构建 PASS（release 无 probe）；设备 A301SO `DebugProbeHostSmokeTest` **OK (2)**：App 分支 2 次 run 新 pid、227 行、persist 文件含 done/line127；**Shizuku 分支 PASS**（shell host uid=2000/`u:r:shell:s0`、243 行、shell persist 路径 best-effort）；`ShizukuFlowHostSmokeTest` **OK (4)**（B3b 新进程 shell 通道：新 pid/旧进程消失/handoff/负例）。
- 证据：review log 2026-11-02 22:45；`/data/user/0/com.ghostlock.app/files/ghostlock-glue/flowhost/debug-probe/debug-probe-last.log` 首尾行已核（含 `debug-probe:done`）。
