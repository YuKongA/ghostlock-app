# CPP12o 真机门禁：Multicast（leak_child 收编 + SESSION-04）— PASS

- 构建：APK 271；native `cd482e9f2d36e37be2dbfb4c33e53fe114af47f324816116b81fc5154e2556a3`
- 入口：Direct（Kotlin 侧 `ksud ready`；完整 W1→W3，`enforce=unreadable` 走 W1 链）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1；运行期间无重启（uptime 29 min）
- 日志：`CPP12o-20260918-multicast-pass.native.log`（190 行）、`CPP12o-20260918-multicast-pass.direct.log`（Kotlin/Direct 侧，208 行）
- 结论：**PASS**；用户确认成功并指示本批免第二次冷机（布局敏感连续复跑要求本批由用户豁免一次）

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；W2 `cred attempt 1/15` 一次命中。
- W1 / W1b scratch repair / W2 / W2b prebuilt repair / W3 TIF_SECCOMP / W3 seccomp mode 全链执行；`threads joined` ×6。
- `child seccomp fully bypassed`、`enforce=1 (enforcing)`、`KernelSU module loaded`、`KernelSU ready`；`exploit complete` T+28428ms（`CPP12n` T+28530ms，差值与冲突搜索波动一致）。
- 唯一告警为基线的 `SELinux enforce unreadable; assuming enforcing and running W1`；无 fallback、无 dirty、无 prepare 重试、无 panic。

## 改动边界（`a2f50f2`）

- `leak_child` 由裸 `pid_t` 收编为 `ghostlock::ChildProcess`；spray 路径的 `waitpid` 回收经 `mark_reaped()` 显式记录，scope 退出不再二次 signal。
- `SESSION-04`：`kernel5_resident_stop` 的 Heap 释放移入 `ExploitSession::release_resident_heap()`，disarm/destroy 顺序不变。
- 二进制：8/8 攻击关键函数形状一致（`do_one_write` 224 条指令仅 1 处地址注解位移）；全量 `shape-different` 仅限 heap/session helper（`prepare_good_kernel_page` +27、`heap_context_init` +22、`~ExploitSession` +11、`run_exploit` -12、`retry_write_stage` -5）。

## 不可判定项

- 设备温度未记录（日志不可判定）；resident 路线未启用。
