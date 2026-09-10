# GhostLock App — Release Notes

## English

Baseline: `7c63bb9` — “Add poco x6 pro (6.1.115 / 6.1.138) offsets (#116)”

### Overall changes

- Application: bilingual Android app with exact `uname -r` matching.
- Main screen: one unified execution button; experimental variants are no longer separate user actions.
- Firmware support: Xperia A301SO 5.15 profile added and marked Shizuku-required.
- Execution state: support card reports unsupported, Shizuku unavailable, permission required, or ready.
- Shizuku: API/provider `13.1.5`, AIDL callback, shell-UID UserService, permission request flow.
- Runtime checks: shell UID, `Seccomp: 0`, exact Xperia kernel, and packaged native binary presence are verified before execution.
- Native payload: one `libghostlock.so` serves the original kernels and selects the Xperia-specific route from the matched profile.
- SELinux: policy-load progress is checked before recovery is treated as successful.
- Diagnostics: native output is streamed to the UI and persisted at `/data/local/tmp/ghostlock-app/.ghostlock_native.log`.
- Existing features retained: CPU-pair selection, safe mode, offset import/export, image and OTA parsing, overwrite handling, execution sheet, and unsupported-kernel rejection.
- Build: macOS NDK detection fixed; Xperia arm64 payload is packaged by the Android build.
- Validation: Debug APK built and installed on the A301SO test device.

### Experimental / incomplete / partially integrated

- Xperia path: device-specific, timing-sensitive, and not yet a general 5.15 implementation.
- Reliability: W1 remains racy; failure can reboot or panic the phone.
- Policy guard: blocks one false-success path but cannot undo corruption that already occurred.
- Shizuku path: functional in Debug, not broadly device-tested, and not fully release/R8-hardened.
- UserService: currently admits only the exact A301SO kernel.
- Multicast writer: recovered for reproducibility; validation is limited to the documented Xperia case.
- Persistent log: improves diagnosis, but hard resets may lose final buffered lines; no post-reboot log browser exists.
- Research archive: historical binaries, patches, logs, and rebase notes are stored outside Git in `ghostlock-xperia-research-archive-2026-09-10.zip`.

<div style="page-break-after: always;"></div>

## 中文

修改自：`7c63bb9`

- 加入基于Shizuku的，适用于 5.15内核 Sony Xperia 1 V A301SO 的支持，计划推广到任意5.x内核


### 实验性 / 半成品 / 部分集成

- Xperia 路径：目前仍是设备专用、依赖时序的实现，尚未推广到通用 5.x 内核。
- 可靠性：W1 仍存在竞争条件，失败可能导致手机重启或 kernel panic。
- 策略保护：只能阻止一条“错误报告成功后继续执行”的路径，不能撤销已经发生的内核破坏。
- Shizuku 路径：Debug 构建可用，尚未完成广泛设备验证，也未完全完成 release/R8 加固。
- UserService：目前只允许精确的 A301SO 内核。
- Multicast writer：为保证可重现性而恢复，目前仅验证过文档记录的 Xperia 场景。
- 持久化日志：改善诊断，但硬重启可能丢失最后缓冲内容，应用尚无重启后日志浏览器。
- 研究归档：历史二进制、补丁、日志和变基说明已移出 Git，保存在 `ghostlock-xperia-research-archive-2026-09-10.zip`。
