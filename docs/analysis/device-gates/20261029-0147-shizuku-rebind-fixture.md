# 20261029-0147 Shizuku rebind / Glued fixture — PASS

对应工作树：`/Users/nickji/.codex/worktrees/exec-glue-clean/ghostlock-app`；基线 HEAD `b119085915748a940740d7ce919b5a30ed8e9346`。验证窗口：2026-10-29 01:35–01:47 America/Toronto。

## 设备与 APK

| 项目 | 结果 |
|---|---|
| A301SO | Android 15 / API 35；kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557` |
| route | Shizuku binder 可达、permission granted；每次 instrumentation 均要求 `expected_route=SHIZUKU` |
| final production APK | `2.0.12 (1342)`；SHA-256 `50073e81341a1c4018240af39d75e7d678a23e319833f8f0c773130dd5cf080a` |
| safe fixture APK | SHA-256 `89361ed79807e3087ceb6d12a39219a5004af3560e07ee102edfd037c472f193`；仅替换两个 Glued native APK entries 为 7,184-byte fixture stub，其余 ZIP 内容与同次构建相同 |

Fixture binaries 只读取 stdin 并输出固定 marker/参数计数；不写文件、不联网、不 fork。测试只通过真实 App Flow/launcher/Shizuku UserService/host/AIDL 执行这些 stub。

## 修复前可重复失败与诊断

命令：

```sh
adb -s adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp shell am instrument -w -e expected_route SHIZUKU -e class com.ghostlock.app.backend.core.GluedExecFixtureSmokeTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner
```

结果：43284 首项通过；紧接的 43499 返回 `exec:shizuku:prepare:-8`，elapsed `5078 ms`；JUnit `Tests run: 2, Failures: 1`。`am instrument` shell exit 为 0，故以 JUnit 文本判定而非 shell code 判定。

A301SO logcat (`../exec-glue/transaction-shizuku-rebind-20261029/BASELINE_FILE/a301so-rebind-logcat.txt`) 显示第二个 UserService record 在 01:35:42.916 创建，binder 于 01:35:43.422 到达 Shizuku server；app 没有进入第二次 `onServiceConnected`，5 秒后 `lastReason()` 再 bind 才看到 `Found existing service record`。Shizuku API 13.1.5 `ShizukuServiceConnection.died()` 会把 callback、connections 清理、tag-cache 移除异步投到 main Handler；修复前客户端在 remove=true 后立即返回，下一次同 tag bind 可撞上缓存连接的清理窗口。

## 最终实现与重复门

`ShizukuBackendClient.disconnect(remove=true)` 对已连接的 service 建立 completion latch；`onServiceDisconnected` 将 latch countdown 排到同一 main looper 的后续队列轮次，以等待 Shizuku 的 death callback 完成缓存移除。等待上限 5 s；未连接不等待。Bind error 记录在 client 本地，`lastReason()` 不再为了取远端 reason 隐式重试 bind。稳定 tag、每 run UserService destroy、单 launcher owner 均保持。

修复后，使用 final source APK 派生 safe fixture，连续执行 10 次相同全类命令；每一次均输出 `OK (2 tests)`、shell exit 0，10/10 PASS。每次都要求 route=SHIZUKU；每组内 43284 与 43499 均 exit-0。输出文件：`../exec-glue/transaction-shizuku-rebind-20261029/MODIFIED_FILE/final-repeat-01.log` 至 `final-repeat-10.log`。每组 marker 包含 43284 `argv_count=8|stdin_bytes=0`、43499 `argv_count=1|stdin_bytes=1884`。最终 logcat 剩余可读的 18 条终态记录中，两 backend 各 9 条均 `terminal=exit-0 fixture=true`，无失败行。

## 结束态与范围

- 测试结束后 A301SO fixture APK 被 final production APK 覆盖；`adb pull` 的 installed `base.apk` 与本地 final APK 逐字节相同。`com.ghostlock.app.test` 已卸载。
- A301SO 仅有 Shizuku server / manager 留存；无 Ghostlock UserService、fixture child 或 fixture ELF process。AVD 同 SHA APK 已安装；Paths UI 截图/XML：`../exec-glue/transaction-shizuku-rebind-20261029/MODIFIED_FILE/avd-paths.png`、`avd-selector.png` 与对应 XML。selector 两项 Glued enabled，原版 43284/98260/43499 与 Custom disabled。
- 主机门：修复前 full unit tests 414 / 0 failures/errors/skipped；最终 full unit tests 同为 414 / 0 failures/errors/skipped；`assembleDebug` 与 `assembleDebugAndroidTest` 成功。
- 此设备门只覆盖 launcher rebind 和 no-op child。它未验证 43499 Finish 与 upstream `ops.cpp` parity/磁盘桥接、fork/setsid descendant cancel/cleanup，也未运行上游漏洞 ELF；不据此宣称整个部署链完成或 exploit supported。
