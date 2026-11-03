# 2026-11-02 B5c-1 设备回归：单一新进程执行通道（旧 runner/双执行体删除后）

- 变更：删除旧 in-process Flow runner / 双执行体通道（`BackendFlowRunner`、`FlowLaunchParent` 旧执行器、`BackendSpec.runner/appOwnedFlowWithAutomaticProcessLauncher`、6 spec runner override）；dispatcher 只剩 `FlowLaunchParent.launchSymbol` → App/Shizuku launcher。另修 release 编译：`defaultConfig` 增 `BuildConfig.M0_MARKER=false`（M0 fixture 仍仅 debug）。
- 设备：A301SO（`QV770MFGJ1`）Android 15 / `5.15.189` / `Enforcing` / `RUNNING_UNLOCKED`。
- 备份（仓外）：`/tmp/ghostlock-b5c1-backup-20261102T195528Z/`，`MANIFEST.sha256` sha256 `a833dab0c107c73a4e97ce714abe46a8d398bc341620d3c6071a7311eb997d01`（19 个删除/改动文件）。
- 产物：debug APK `b64237c4f9272b9edadd327a0a595be20f350e5fad96d1ec23eb082ea3686e2b`；fixture APK `31c21fb4d4058489f8860d6bd4dc32e86b5986730e904d6ba0e8a53806c3317c`；androidTest APK `e0adf4a02278cf571d1e4997b981851da27bb3b953e084751d40724d60617de2`；**release APK** `4b3f64071589d61eca055a09336e841303121d3f79eee0c1efdf555701db6713`（构建产物，未安装运行）。

## 命令与结果

```text
# 全量单测（Lead 独立复跑）
./gradlew :app:testDebugUnitTest --no-daemon --console=plain → BUILD SUCCESSFUL in 41s / EXIT=0
  （agent 报告 XML：tests=486 failures=0 errors=0 skipped=0）

# 生产 APK（App 通道）
adb install -r -t <debug.apk>                                → Success
am instrument …FlowHostProcessSmokeTest …                    → OK (4 tests)  Time 7.480  EXIT=0

# fixture APK（生产 entry，no-op ELF）
pm clear com.ghostlock.app ; install fixture + test APK      → Success
am instrument …Flow43499GluedHostSmokeTest …                 → OK (1 test)   Time 4.325  EXIT=0
am instrument …Flow43284GluedHostSmokeTest …                 → OK (1 test)   Time 4.118  EXIT=0

# 恢复生产 APK + 回归
adb install -r -t <debug.apk>                                → Success
am instrument …FlowHostProcessSmokeTest …                    → OK (4 tests)  Time 7.412  EXIT=0
```

## 判据

| 判据 | 结果 | 证据 |
|---|---|---|
| 单一执行通道（无旧 runner/双执行体） | PASS（构建/单测） | 全量单测 486/0/0；grep main 旧执行器符号 0 代码引用；dispatcher 只走 launchSymbol |
| App `:flowhost` 通道 + 诊断 fixture | PASS | B2 smoke OK(4)（生产 APK） |
| 43499-Glued 生产 entry + registry | PASS | B4a fixture OK(1) |
| 43284Glued 生产 entry + IPsec runtime | PASS | B4b fixture OK(1) |
| 恢复生产 APK 后回归 | PASS | B2 OK(4) |
| host 进程回收 | PASS | 门禁后 `ps -A | grep flowhost` 为空（exit 1） |
| release 构建 | PASS（构建） | `:app:assembleRelease` EXIT=0（M0_MARKER 修复后） |

## UNVERIFIED

- release APK 未安装/运行（R8/minify、M0_MARKER=false 真机语义未验证）。
- gate 语义收紧（未授权 Shizuku 不再被 Glued 的 appOwned 旁路跳过）仅有单测；A301SO 无 Shizuku，未做设备未授权用例。
- B5c-2 未做：旧 `ExecutableBackendProcess`/`ExecutableShizukuHost`/`Glued*ExecutableHost`/`ShizukuBackendHost(s)`/旧 AIDL host 方法/`Flow43499Glued`/`Flow43284Glued` 仍为死代码。
- Lua/动态 backend 无 flowSymbol ⇒ `NO_FLOW_RUN`（B5b 注册路径未做）；B4-real 真实漏洞冷机门禁仍待用户授权。
- 若回归失败，回退依据：上述 /tmp 备份 MANIFEST（仓外，未入库）。
