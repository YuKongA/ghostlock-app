# 2026-11-02 B4a-b 设备门禁：43499-Glued 生产 Flow 经新进程通道（fixture no-op ELF，A301SO）

- 计划：`docs/plan/flow-process-launcher-plan.md` v5 + 附录 `flow-process-launcher-annex-b4a.md`（host 注入参数 / context-free entry）
- 设备：A301SO（`QV770MFGJ1`），Android 15，kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，`Enforcing`，`RUNNING_UNLOCKED`
- 工作树：HEAD `b1190859` + 未提交新架构工作树；`versionName=2.0.12/versionCode=1342`

## 产物身份（sha256）

- 生产 debug APK（构建）：`build/app/outputs/apk/debug/GhostLock-v2.0.12(1342)-arm64-v8a-debug.apk` = `b51c757931547a8e18545861a27c638463cc959786f0608cbb090e5feb325dc3`
- **fixture APK**（仅替换两个 Glued ELF 为 no-op fixture、保留 `resources.arsc` Stored、重签）= `/tmp/GhostLock-b4a-fixture.apk` = `a7305ab8dfa5eb05b149c122dda75d5f47f39a40989577a7fc50338998538ec3`
  - 两个 ELF 均含 `fixture=ghostlock-exec-noop-v1` 标记；fixture 来源 `docs/exec-glue/transaction-safe-exec-fixture/MODIFIED_FILE/`
- androidTest APK = `1986a5ece387736ad903bd27ebc72a808f80267e5c39ec5eaa92488f78264c40`
- 门禁后已**恢复生产 APK**（重装 `b51c7579…` 成功，见下回归）

## 命令与结果

```text
adb -s QV770MFGJ1 shell pm clear com.ghostlock.app            → Success（清 prefs，执行体默认 App）
adb -s QV770MFGJ1 install -r -t /tmp/GhostLock-b4a-fixture.apk → Success
adb -s QV770MFGJ1 install -r -t <androidTest.apk>              → Success
adb -s QV770MFGJ1 shell am instrument -w -e class \
  com.ghostlock.app.backend.core.Flow43499GluedHostSmokeTest \
  com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner
→ com.ghostlock.app.backend.core.Flow43499GluedHostSmokeTest:.
  Time: 4.351
  OK (1 test)
  EXIT=0
（复跑一次：OK (1 test)，EXIT=0）
```

## 判据与证据

| 判据（附录 §4 / 计划 B4a） | 结果 | 证据 |
|---|---|---|
| gate allowed（43499-Glued 仍 Ready） | PASS | 测试断言 `BackendRunGate.decide(...).allowed` |
| dispatcher 走新通道并 Executed | PASS | 测试断言 `BackendRunDispatch.Executed` |
| Flow 终态 `Finished.Ok` | PASS | 测试断言 |
| 生产 entry 被执行：输出含 fixture 标记 | PASS | `firstOutput.any { it.contains("fixture=ghostlock-exec-noop-v1") }` |
| host 进程 ≠ app 进程 | PASS | `evidence.host_pid` 非空且 ≠ `Process.myPid()` |
| `evidence.host_run_dir` 非空 | PASS | 测试断言（host 注入的 run dir 路径） |
| 两次 run 使用不同 host 进程 | PASS | 两次 `evidence.host_pid` 不同 |
| fixture 仅 no-op，不运行真实漏洞 ELF | PASS | fixture ELF 只读 stdin/输出固定 marker；生产 ELF 未执行 |
| host 进程回收 | PASS | 门禁后 `ps -A | grep flowhost` 为空（exit 1） |
| 恢复生产 APK 后回归 | PASS | 重装生产 APK（sha `b51c7579…`）后 `FlowHostProcessSmokeTest` = **OK (4 tests)** / 7.36s / EXIT=0 |

原始输出：`INSTRUMENT.txt`、`LOGCAT.txt`（同目录）。

## UNVERIFIED（不得据此标 supported）

- **真实漏洞 ELF** 冷机门禁（B4-real）未执行（需用户明确授权，另立门禁）。
- 43284Glued/98260/Lua 尚未迁移（B4b/B5）——本批只迁 43499-Glued 且为单后端单变量。
- Shizuku flavor（B3b）：A301SO 无 Shizuku 包；`host.native_lib_dir` shell 可解析性、shell run_dir `/data/local/tmp` 0700/SELinux 未验。
- 超时/取消路径（entry `waitFor` 有界轮询已验证代码，未在设备触发）；A11“超时不 kill 载荷”行为未实测。
- fixture/profile 传递 ≥256 KiB 预算拒绝未触发；大输出/大 stdin 死锁未实测（no-op fixture 极小）。
- 同 versionCode fixture→生产 APK 替换已验证可安装/回归；未验证 Play 签名/生产发布路径。
