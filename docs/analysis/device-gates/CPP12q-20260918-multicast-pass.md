# CPP12q 真机门禁：Multicast（SESSION-02）隔离复跑 — PASS

- 构建：APK 274；native `395c5faf2eb879fa0abb0ea47291c51ba75b54a7c1bdfec53e1335d472e987fe`
- 入口：Direct（`ksud ready` 前置；完整 W1→W3，`enforce=unreadable` 走 W1 链）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1；运行期间无重启（uptime 4 min）
- 日志：`CPP12q-20260918-multicast-pass.native.log`（190 行）、`CPP12q-20260918-multicast-pass.direct.log`（208 行）
- 结论：**PASS（隔离复跑）**；同一构建在 `CPP12p` 崩溃一次后复跑通过

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；W2 `cred attempt 1/15` 一次命中。
- W1 / W1b scratch repair / W2 / W2b prebuilt repair / W3 TIF_SECCOMP / W3 seccomp mode 全链执行；`threads joined` ×6。
- `child seccomp fully bypassed`、`enforce=1 (enforcing)`、`KernelSU ready`；`exploit complete` T+28231ms（`CPP12o` T+28428ms、`CPP12n` T+28530ms，处于基线波动区间）。
- 唯一告警为基线的 `SELinux enforce unreadable; assuming enforcing and running W1`；无 fallback、无 dirty、无 prepare 重试、无 panic。

## 判定

- 同一二进制 `395c5faf…`：`CPP12p` 一次 W1 PI-route 窗口 panic、`CPP12q` 一次完整 PASS，符合 `KERNEL-PANIC-01` 的"同一二进制可一次崩溃、一次通过"特征；崩溃点不在 SESSION-02 改动函数的执行路径上。
- 按布局敏感规则仍需第二次冷机（KernelSU 加载后必须 reboot 回干净启动）。
