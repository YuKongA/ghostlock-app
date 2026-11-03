# 2026-11-02 B5c-2 设备回归：删除 43499/43284 旧 ELF exec 通道后（单一新进程通道）

- 变更：删除 43499/43284 旧 ELF exec 通道 19 个文件（`ExecutableBackendProcess`/`ExecutableShizukuHost`/`ExecutableRunFileStager`/`ExecutableRunEnvironment`/`ExecutableHostProtocol`/`Utf8LogLineAssembler`/`Glued*ExecutableHost`/旧 `Flow43499Glued`/`Flow43284Glued` + 5 单测 + 4 androidTest）；`ShizukuBackendHost` 仅移除 Glued wire id/create 分支，保留 98260 host 基础设施与 AIDL。旧通道在 main/test/androidTest 的编译引用 = 0。
- 备份（仓外）：`/tmp/ghostlock-b5c2-backup-20261102T200303Z/`，`MANIFEST.sha256` = `8ae0a112a98a79fa8848b1f5e1fbf2d0e0f5606468916a7b4ae59694abda8383`（20 文件）。
- 设备：A301SO（`QV770MFGJ1`）Android 15 / `5.15.189` / `Enforcing` / `RUNNING_UNLOCKED`。
- 产物：debug APK `17a419372263aa34b129b70c88a2bbaa05274d6f433ad3360409b75dc6eb4035`；fixture APK `b9af596e8014780ae0650bd8e8cf3710ab655c70dcfa22d263926e952ae70858`；androidTest APK `e7bae11c2404247713236965105bd5d90502f4769d04c5eb016b019bf7c14d18`；release APK `2fa9fc68bbec063ab47587e6d956aabd184e637eac690d08fcc578cde23ddf02`（构建，未运行）。

## 门禁与结果

```text
# Lead 串行门禁（clean 后单 Gradle 调用，避免共享 build 目录并发）
./gradlew :app:testDebugUnitTest :app:assembleDebug \
  :app:assembleDebugAndroidTest :app:assembleRelease --no-daemon --console=plain
→ BUILD SUCCESSFUL in 1m 8s / EXIT=0
XML: tests=470 failures=0 errors=0 skipped=0

# 设备
adb install -r -t <debug.apk> / <androidTest.apk>              → Success
am instrument -w -e class …FlowHostProcessSmokeTest …         → OK (4 tests)  Time 7.624  EXIT=0
pm clear ; install fixture APK                                 → Success
am instrument -w -e class …Flow43499GluedHostSmokeTest …      → OK (1 test)   Time 4.362  EXIT=0
am instrument -w -e class …Flow43284GluedHostSmokeTest …      → OK (1 test)   Time 4.056  EXIT=0
adb install -r -t <debug.apk>（恢复生产）                       → Success
am instrument -w -e class …FlowHostProcessSmokeTest …         → OK (4 tests)  Time 7.430  EXIT=0
```

| 判据 | 结果 | 证据 |
|---|---|---|
| 全量单测（旧通道删除后） | PASS | XML 470/0/0/0；EXIT=0 |
| debug/androidTest/release 构建 | PASS | 单 Gradle 调用四任务 EXIT=0 |
| App `:flowhost` 通道 + 诊断 fixture | PASS | B2 OK(4)（生产 APK） |
| 43499-Glued 生产 entry + registry | PASS | B4a fixture OK(1) |
| 43284Glued 生产 entry + IPsec runtime | PASS | B4b fixture OK(1) |
| 恢复生产 APK 后回归 | PASS | B2 OK(4) |
| host 进程回收 | PASS | `ps -A | grep flowhost` 为空（exit 1） |

## 附注（构建并发事故与修复）

Lead 首次并行运行 `testDebugUnitTest`（后台）与 `assembleDebug` 导致共享 build 目录竞争、两任务均 BUILD FAILED；随后 `./gradlew clean` 并改为**单 Gradle 调用串行执行**全部门禁，全部 PASS。并发运行 Gradle 门禁属流程错误，记录以免复现。

## UNVERIFIED

- release APK 未安装/运行（R8/minify、M0_MARKER=false 真机语义）。
- 98260 legacy Shizuku host 通道（本轮未动）未做设备回归；`ShizukuBackendHosts` 不再接受 Glued wire id（仓内无调用者）。
- `ExecutableProcessRouting.kt` main 已无生产调用者（候选 B5c-3）。
- B4-real 真实漏洞冷机门禁仍待用户明确授权（见 `docs/plan/flow-process-launcher-annex-b4-real.md`）。
