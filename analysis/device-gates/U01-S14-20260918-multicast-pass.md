# U01-S14 真机门禁：W3 child retirement 移植（Multicast）— PASS（冷机）

- 构建：APK 328；native `88390be714bdce0a`（`clear_victim_seccomp` 的 leaf-dir probe 失败不再
  猜测方向盲写，改为返回失败交由 `run_w2_w3_chain` 下一轮新 child；相对 `55863311` 仅
  `run_exploit` +3 指令，8 个攻击关键函数 shape 不变）
- 入口：Direct；`boot_ms=42755`（冷启动后运行）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`U01-S14-20260918-multicast-pass.native.log`（235 行）
- 结论：**PASS（冷机；常规 W3 路径）**

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0`；W1 页验收 `attempt=4 +12801ms`，其余 5 次 `attempt=1`。
- `Write 1 complete` T+19331ms、`exploit complete` T+39276ms。
- W3 `TIF_SECCOMP+mode attempt 1/6` 直接命中（`tcp_route_selected()==true`）。
- `KernelSU module loaded`、`enforce=1 (enforcing)`、`KernelSU ready`；pstore console 无 panic 关键字。

## 判定与限制

- retirement 的**失败分支本次未被触发**：`W3-0: leaf dir` probe 只在 `tcp_route_selected()==false`
  时执行，而生产配置（profile `tcp_zerocopy_enabled` + 5.15 TCP capability）下该值为 true。
- 历史核对：`analysis/device-gates/*.native.log` 中 `W3-0: leaf dir` 出现次数为 **0**，即该 probe
  分支在既有门禁中从未在生产配置下执行。移植语义正确性由代码审查与 upstream `U01` 对齐保证；
  若需覆盖失败分支，需专门以 `tcp_route_selected()==false` 配置运行（待定）。
- 逐次 KernelSU 日志路径仍为未移植项（代码 `TODO(U01-S14-KSU-LOG)`）。
