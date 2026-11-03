# 2026-11-02 B2b 设备门禁：`:flowhost` 新进程 Flow 通道（A301SO）

- 计划：`docs/plan/flow-process-launcher-plan.md` v5（§5 B2；D1=A manifest `:flowhost` Service）
- 设备：A301SO（`QV770MFGJ1`），Android 15，kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，`Enforcing`，用户 `RUNNING_UNLOCKED`，boot_id `5c2b6cee-…`，uptime 5529s
- 工作树：HEAD `b1190859`（dev-nocpp）+ 未提交 exec-glue/新架构工作树；`versionName=2.0.12/versionCode=1342`
- APK（debug）：`build/app/outputs/apk/debug/GhostLock-v2.0.12(1342)-arm64-v8a-debug.apk`，sha256 `38649ed796ec61813ab5324fd3a75278c261d10cce000a7f23b8bda6aeb319f4`，含 `lib/arm64-v8a/lib43499-Glued.so`、`lib/arm64-v8a/lib43284Glued.so`、`:flowhost` service
- Test APK：`build/app/outputs/apk/androidTest/debug/GhostLock-v2.0.12(1342)-debug-androidTest.apk`，sha256 `2d9db0037d30995e501bf9fe1aedd6f77c3c495d723a94ffe083f2e971b62ca6`

## 命令与结果

```text
adb -s QV770MFGJ1 install -r -t <debug.apk>      → Success
adb -s QV770MFGJ1 install -r -t <androidTest.apk> → Success
adb -s QV770MFGJ1 shell am instrument -w -e class \
  com.ghostlock.app.backend.core.FlowHostProcessSmokeTest \
  com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner
→ com.ghostlock.app.backend.core.FlowHostProcessSmokeTest:....
  Time: 7.487
  OK (4 tests)
  EXIT=0
（复跑一次：Time 7.34 / OK (4 tests) / EXIT=0）
```

原始输出：`INSTRUMENT.txt`（两次运行）与 `LOGCAT-FLOWHOST.txt`（同批 logcat）。

## 判据与证据

| 判据（计划 B2） | 结果 | 证据 |
|---|---|---|
| DiagnosticOkFlow：Finished.Ok + 3 行 `diag:line:*` | PASS | 测试断言；`LOGCAT-FLOWHOST.txt` TestRunner finished |
| 每次 run 新 host 进程、旧进程消失 | PASS | 测试断言 `lastHostPid` 不同 + `/proc/<old>` 不存在；logcat 见 `:flowhost` pid 28185 / 28202 依次 spawn→died |
| 非法 symbol 拒绝（具名） | PASS | `Refused` 含 `symbol`；host log `prepare/start failed: flow-symbol:unregistered` |
| payload 超限拒绝（具名） | PASS | `Refused` 含 `payload`（`flow-launcher:app:payload-too-large`） |
| handoff 不报成功 | PASS | `DiagnosticHandoffFlow` → `BackendRunUiState.HandoffRequested`（非 Finished/Ok） |
| 真实 Binder Bundle wire（params/payload/logFd） | PASS（app 路径） | prepare 跨进程传 `Bundle/byte[]/ParcelFileDescriptor?` 全部调用成功 |
| 一次性/终态回收 | PASS | host 终态后 `stopSelf`+`killProcess`；run 结束后 `ps -A | grep flowhost` 为空；AMS 无遗留活跃进程 |

补充：`FlowHostProtocolTest`（JVM）9 tests/0 failures 已在本批门禁；APK merged manifest 含 `com.ghostlock.app.backend.core.FlowHostService`（`:flowhost`，`exported=false`、`stopWithTask=false`）。

## 日志摘录（设备）

```text
I/ck.app:flowhost(28185): Late-enabling -Xcheck:jni
I/ActivityManager: Process com.ghostlock.app:flowhost (pid 28185) has died: vis FGS
I/ActivityManager: Start proc 28202:com.ghostlock.app:flowhost/u0a673 for service {com.ghostlock.app/…FlowHostService}
W/GLK-FlowHost(28202): prepare/start failed: flow-symbol:unregistered
I/ActivityManager: Process com.ghostlock.app:flowhost (pid 28202) has died: vis FGS
I/TestRunner: finished: invalidSymbolIsRefused(...)
```

## UNVERIFIED（不得据此标 supported）

- `cancel()` 实测（smoke 未覆盖取消路径）；`stopWithTask`/task 移除/bind 超时/同 UID 进程复用显式注入。
- UI 集成（FlowLaunchParent/BackendSpec/`FlowLaunchParent.launch` 返回 `HandoffRequested`）属 B4a；Shizuku 进程 flavor 属 B3。
- 生产攻击 flow（43499-Glued/43284Glued）未在本通道执行；本门禁只用无副作用诊断 fixture。
- 低内存/AMS 重启时序、logFd 真实写盘（本批 launcher 传 null logFd）、崩溃注入。

## 附注

- `GhostLock` 日志中 `libghostlock_jni/43499/43284.so` dlopen 失败为 exec-glue 构建的既有预期（旧 native 目标不构建），不影响 `:flowhost` 通道；`lib43499-Glued.so` 已打包（B4a 使用）。
- 门禁仅证明“新进程 + reflection Flow 通道”成立，不构成应用/攻击路径 supported 结论。
