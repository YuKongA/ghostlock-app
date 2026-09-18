# CPP08 Direct 门禁通过 + Shizuku 门槛失败（同批记录）

## Direct：通过

- APK：`versionCode=206`（含 `8bfcffd` 的 fork+exec EFAULT 修复与 handoff 诊断；U01-H 的 iomem 缓存在 208 才进入）
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Direct；CPU main=0、consumer=1
- 温度：日志不可判定
- 结论：通过

验证要点：

- handoff 诊断确认修复生效：`handoff: script open fd=3 errno=0`、`handoff: root shell worker pid=…`；
- `[*] root script start uid=0`、`policy fixup rc=0`、`late-load exit=0`、`KernelSU module loaded`、`KernelSU ready`、`enforce=1`；
- 6 次 route 全部 `route_done status=0 clean=1/1`；6 次 `prepare_kernel_page ok attempt=1`，无页面验收重试；
- collision `1936–2002ms`、leak `≈2.0s`，与基线一致。

## Shizuku：首测被硬门槛拒绝（已修复）

- APK：`versionCode=208`；执行 21:25:03，失败日志仅两行：
  `error: kernel does not require Shizuku: 5.15.189-…`
- 根因：`GhostlockUserService.runExploit()` 使用
  `require(resolvedProfile.optInt("requires_shizuku") == 1)` 拒绝执行；用户手动开启 Shizuku 开关时该字段为 0。
- 修复：该字段降级为日志信息，不再作为门槛（`requires_shizuku` 依 `PROFILE-SUGGEST-01` 作为建议值）；
  uid、seccomp、release 三项执行前置检查保留。
- 待 `versionCode=209` 复测。

原始日志：

- Direct：[`CPP08-20260917-direct-pass.native.log`](CPP08-20260917-direct-pass.native.log)
- Shizuku 门槛失败：[`CPP08-20260917-shizuku-gate-fail.native.log`](CPP08-20260917-shizuku-gate-fail.native.log)
