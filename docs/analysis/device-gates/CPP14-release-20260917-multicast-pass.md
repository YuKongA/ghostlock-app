# CPP14 Release 门禁：Multicast（Release APK 复跑）— PASS

- 构建：`GhostLock-v1.1(256)-arm64-v8a-release.apk`；native `625d5300f7312a44de802a3f48ab3757ce6d626c192c2109ed27c7280d654252`（与 `CPP12m`/`CPP12n` Debug 门禁版逐字节相同）
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`；CPU main=0 consumer=1
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 日志：`CPP14-release-20260917-multicast-pass.native.log`（211 行；release APK 不可 `run-as`，复装同签名 debug APK 后从私有目录导出）
- 结论：**PASS**（Release 首次运行失败为攻击层间歇性，非 Release 打包问题）

## 覆盖点

- 8/8 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`：W1、W1b、W2（attempt 1/15 一次命中）、W2b、W3 `TIF_SECCOMP`+seccomp mode ×2 轮（第一轮未通过，链式重试第二轮成功）。
- 8 次 spray 全部 `prepare_kernel_page ok attempt=1`；`threads joined` ×8。
- `runtime verbose_debug=0`（Release 无 Debug execution 快照，符合设计）；R8 未破坏启动协议与日志路径。
- `child is root!`、`enforce=1 (enforcing)`、`KernelSU ready`；`exploit complete` T+41610ms。
- 无 fallback、无 dirty、无 panic。

## 与首次失败的关系

- 首次运行（`CPP14-release-20260917-multicast-crash`）在 W1b spray 后设备重启，日志截断；native 与本次逐字节相同，判为 `KERNEL-PANIC-01` 的间歇性命中。
- 复跑通过后，Release 构建链与运行时链路均验证完成；Release 证据与 Debug 门禁（`CPP12m/n`）行为一致。

## 不可判定项

- 设备温度未记录（日志不可判定）；resident 路线未启用；TCP/Select 仍待外部设备。
