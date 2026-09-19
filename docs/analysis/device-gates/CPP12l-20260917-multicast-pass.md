# CPP12l 真机门禁：Multicast（CPP12k 回退后隔离复跑）— PASS

- 构建：APK 244；native `cea1bedc5f085778489bff88d86893c6ab5bd643230764b899d196fbacfc9172`（与 CPP12j 通过版本逐字节一致；`552c8b2` 回退 `c222151`）
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`（完整 W1→W3）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12l-20260917-multicast-pass.native.log`（206 行）
- 结论：**PASS**。设备与攻击层在 CPP12k 两次 panic 后仍稳定；CPP12k 的两次 panic 与 `b147df9f`（VictimContext）构建相关，已回退

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`：W1、W1b、W2（attempt 1/15 一次命中）、W2b、W3 `TIF_SECCOMP`、W3 seccomp mode。
- 6 次 spray 全部 `prepare_kernel_page ok attempt=1`；`threads joined` ×6。
- `private scratch repaired; releasing quarantine`、`child is root!`、`child seccomp filter bypassed`、`handoff: child=24455 alive=1 sent=1 errno=0`、`enforce=1 (enforcing)`、`KernelSU ready`。
- `exploit complete` T+32715ms；无 fallback、无 dirty、无 panic。

## 回退记录

- `c222151`（`child_pipes` → `ghostlock::VictimContext`）在 APK 242 / native `b147df9f` 上连续两次 panic（证据 `CPP12k-20260917-multicast-kernel-panic.md`），两次均截断在 W2 spray 窗口。
- 依 `PI-TIMEOUT-01` 先例回退（`552c8b2`），重建 native 与 CPP12j 通过版本逐字节一致；本日志即隔离复跑证据。
- VictimProcess 收编登记为待受控重试项；重试前需要先解决/绕开布局敏感性与 W2 spray 窗口的间歇性内核崩溃问题（`KERNEL-PANIC-01`）。

## 不可判定项

- 设备温度未记录；两次 panic 与本次复跑均未记录冷却间隔（日志不可判定）。
