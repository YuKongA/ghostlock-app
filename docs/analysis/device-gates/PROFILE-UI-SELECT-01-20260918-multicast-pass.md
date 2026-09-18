# profile-ui + SELECT-01 真机门禁：Multicast 回归 — PASS（冷机）

- 构建：APK 338；native `e13ed9ddeeb128d0`（执行参数编辑 UI + compact Select 路线内重试）
- 入口：Direct；`boot_ms=63079`（冷启动后运行）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`PROFILE-UI-SELECT-01-20260918-multicast-pass.native.log`（210 行）
- 结论：**PASS（Multicast 回归 + UI 目视确认）**

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0`；`Write 1 complete` T+11375ms。
- `KernelSU module loaded`、`enforce=1 (enforcing)`、`KernelSU ready`；pstore 无 panic。
- `debug.execution.*` 全部为 shipped defaults（W1=15、W2=15、W3 chain=3、W3=6、
  route_wait=1000、heap attempts=4 等），说明无 override 时 resolved profile 不变。
- 用户已在高级区目视确认新增的执行参数编辑/推荐核心/保存清除界面。

## 未覆盖 / 限制

- **override 保存/清除路径未在本次触发**：设备上不存在 `offsets.json`（用户未保存过），
  UI 编辑→持久化→下次运行生效的闭环未由真机覆盖；由主机编译与代码路径审查保证。
- **SELECT-01 未被本次运行覆盖**：Multicast 路线不进入 Select；compact 4 次重试与页重建
  无 Select 设备可验证（计划中按此口径登记）。
- 形状对比基线 `3d7c024d`：`do_pselect_fake_lock_route` 859→657（execute 内联后的重写）、
  `open_selected_fds`/`select_stack_build_fdsets` 由内联转为独立符号；8 个攻击函数 shape 不变。
