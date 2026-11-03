# 2026-11-02 release APK 启动 smoke（A301SO，非攻击）

- 目的：验证 `:app:assembleRelease`（R8/minify、`M0_MARKER=false`）产物可安装并启动，不替代真机攻击门禁。
- 设备：A301SO（`QV770MFGJ1`）Android 15 / `5.15.189` / `Enforcing` / `RUNNING_UNLOCKED`。
- 产物：release APK sha256 `2fa9fc68bbec063ab47587e6d956aabd184e637eac690d08fcc578cde23ddf02`（3,553,943 B）；签名证书与 debug APK 相同（Android Debug 证书 `b92a1aa5…`，故可覆盖安装）。

## 命令与结果

```text
adb install -r -t <release.apk>                     → Success
adb shell am force-stop com.ghostlock.app
adb shell am start -n com.ghostlock.app/.ui.MainActivity
  → Starting: Intent { cmp=com.ghostlock.app/.ui.MainActivity }
logcat: ActivityTaskManager: Displayed com.ghostlock.app/.ui.MainActivity for user 0: +1s127ms
adb shell pidof com.ghostlock.app                   → 4002（进程存活）
crash scan（FATAL EXCEPTION/AndroidRuntime Process: com.ghostlock）→ 无
# 恢复
adb install -r -t <debug.apk> ; adb install -r -t <androidTest.apk> → Success
```

## 判据

| 判据 | 结果 | 证据 |
|---|---|---|
| release APK 可安装（同 debug 证书覆盖） | PASS | install Success |
| MainActivity 可启动并显示 | PASS | `Displayed … +1s127ms` |
| 主进程存活、无启动崩溃 | PASS | pid 4002；logcat 无 FATAL/进程崩溃 |
| 恢复 debug + test APK | PASS | 重装 Success |

## UNVERIFIED

- release 构建下的 `:flowhost` 新进程通道/FlowEntry/IPsec 未执行（release app 不可 instrumentation；需 UI 触发攻击流程，属 B4-real/后续授权范围）。
- R8 对反射 registry/host 类裁剪的运行时行为未验证（本次仅主进程启动）。
- 真实漏洞 ELF 冷机门禁（B4-real）仍待用户授权（`docs/plan/flow-process-launcher-annex-b4-real.md`）。
