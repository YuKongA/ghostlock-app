# 2026-11-02 G13 设备回归：绑定期可取消 + Shizuku keepalive unbind 修复（App 通道全回归）

- 变更（G13）：`AppProcessFlowLauncher`/`ShizukuFlowHostClient` 的绑定等待从 `CountDownLatch.await(10s)` 改为 **suspend 轮询**（25ms + `ensureActive`，上限 10s），绑定期取消立即传播；`ShizukuFlowHostClient` 在 terminal evidence `host_keepalive=true` 时 **`unbindUserService(..., remove=false)`**，保留 shell UserService/进程（A11），非 keepalive 仍 remove=true；Shizuku 绑定期取消先 unbind 再 rethrow。
- 设备：A301SO（`QV770MFGJ1`）Android 15 / Enforcing / `RUNNING_UNLOCKED`。
- 产物：debug APK `96a3606db6bf6a2b062d4664cf8b96bd39b3a3282bca3c34a5b2be3a8f2abfe3`；androidTest APK `043c35662651492b188f7f217452026226580b6fbeb7665a44d006100eef38bd`；slow fixture APK `8db55d883470a8b0bf005916b2283a30c726fb72d8e8090493e8045166b21241`；normal no-op fixture APK `bc9644499226b7acc20fd55cac0f88433ea39496a620416eba9b86c7e2f62bc8`。
- 备份：`/tmp/ghostlock-g13-backup-20261102T211928Z/`，`MANIFEST.sha256` = `9ddd500693a0f5bfe627b34f5a4c15b144f994bd46f52a9f06af3221989714fa`。

## 门禁与结果

```text
# Lead 串行门禁
./gradlew :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest → BUILD SUCCESSFUL / EXIT=0
XML: tests=470 failures=0 errors=0 skipped=0

# App 通道回归（生产 APK）
am instrument …FlowHostCancellationTest …       → OK (2 tests)  Time 8.312  EXIT=0
am instrument …FlowHostKeepaliveTest …          → OK (1 test)   Time 3.916  EXIT=0
am instrument …FlowHostProcessSmokeTest …       → OK (4 tests)  Time 7.641  EXIT=0

# G12 生产 43284 超时/leak 回归（slow fixture APK）
am instrument …Flow43284TimeoutKeepaliveTest …  → OK (1 test)   Time 5.918  EXIT=0（非 skip）
am instrument …FlowHostProcessSmokeTest …       → OK (4 tests)  EXIT=0

# 常规 no-op fixture 回归
am instrument …Flow43499GluedHostSmokeTest …    → OK (1 test)   EXIT=0
am instrument …Flow43284GluedHostSmokeTest …    → OK (1 test)   EXIT=0

# 恢复生产 APK + B2 + 残留
am instrument …FlowHostProcessSmokeTest …       → OK (4 tests)  EXIT=0
ps -A | grep -E "flowhost|lib43284Glued"        → 无（exit 1）
```

| 判据 | 结果 |
|---|---|
| 绑定期等待改动无回归（取消/keepalive/slow-timeout 路径） | PASS（G10/G11/G12 三测试全绿） |
| 生产 43284 超时 → runtime-leaked/keepalive/A11 payload 存活 仍成立 | PASS（G12 非 skip） |
| 常规 fixture 生产 entry 通道 | PASS（B4a/B4b） |
| 设备无残留进程 | PASS |

## UNVERIFIED

- **Shizuku keepalive 真机语义**（B3b）：设备无 Shizuku 包；`unbind(remove=false)` 是否确实保留 shell 进程并持有 leaked runtime、后续新 run 的唯一 tag/processNameSuffix 兼容性均未验证。
- 绑定期**恰好发生在 bind 窗口内**的取消未被单独计时验证（测试取消发生在 flow 启动后；绑定等待已改为可取消轮询，但该窗口只有代码/编译证据）。
- Shizuku 绑定期取消的 unbind 与 Shizuku tag 缓存异步竞态（B3b 观察项）。
- keepalive 识别仅覆盖 Terminal verify 成功路径；取消/异常路径仍 remove=true（设计如此）。
- keepalive 回收 watchdog 仍缺（技术债）；真实 43284 漏洞 ELF/B4-real 仍待授权。
