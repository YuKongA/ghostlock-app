# CPP12n 真机门禁：Multicast（VictimContext 小步收编，第二次冷机）— PASS

- 构建：APK 247；native `625d5300f7312a44de802a3f48ab3757ce6d626c192c2109ed27c7280d654252`
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`（完整 W1→W3）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12n-20260917-multicast-pass.native.log`（190 行）
- 结论：**PASS**（第二次冷机复跑，布局敏感规则满足）；与 `CPP12m` 同一构建

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；W2 `cred attempt 1/15` 一次命中。
- 6 次 spray 全部 `prepare_kernel_page ok attempt=1`；`threads joined` ×6；`W1b`/`W2b` repair 正常。
- `child is root!`、seccomp bypass、`enforce=1 (enforcing)`、`KernelSU ready`；`exploit complete` T+28530ms（`CPP12m` T+32997ms，差值为冲突搜索波动）。
- 无 fallback、无 dirty、无 panic。

## 结论

- VictimContext pipe 集合收编（`6ab92de`）经两次冷机（`CPP12m`、`CPP12n`）连续通过，判定为稳定改动。
- pid 所有权收编仍按 `CPP12k` 的结论等待受控重试（`KERNEL-PANIC-01`）。

## 不可判定项

- 设备温度未记录（日志不可判定）；resident 路线未启用。
