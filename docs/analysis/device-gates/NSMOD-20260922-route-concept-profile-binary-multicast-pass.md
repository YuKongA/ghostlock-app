# NSMOD 真机门禁：RouteLifecycle concept + profile_binary 类型安全化（Multicast）— PASS

对应提交 `8623598`（`refactor(native): RouteLifecycle concept and type-safe GLK1 field access`）。

## 设备与入口

- 设备：`A301SO`（`adb -s QV770MFGJ1`），kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct（`--load-prebuilt-profile`，uid=2000、`u:r:shell:s0`、`Seccomp=0`），
  home `/data/local/tmp/ghostlock-app`。
- 路线：Multicast（one-shot）。
- CPU：`cpu pair: main=0 consumer=1`。
- 设备状态：冷启动后 `grep -i kernelsu /proc/modules` 为空，`uptime` 全程连续。

## 结果

- 4 次 `route_done` 全部 `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；
  `mcast ghost disarm ret=-1 errno=110`（预期）。
- `child is root!`、`exploit complete`、handoff `KernelSU ready`。
- **无 kernel panic**，`uptime` 连续。

日志：`NSMOD-20260922-route-concept-profile-binary-multicast-pass.native.log`。

## 变更说明（均已在此构建内验证）

- `routes/route_lifecycle.hpp`：`RouteLifecycle` concept + `run_route_lifecycle()`
  取代 `do_tcp_fake_lock_route`/`do_pselect_fake_lock_route` 里重复的
  `prepare→execute→disarm→destroy` 骨架；不引入虚函数/vtable。
- `profile_binary.cpp`：`kFields` 改为「按成员访问表达式生成的类型安全
  load/store 表」，删除 `offsetof + reinterpret_cast`；记录宽度/顺序与符号处理
  不变，host `profile_binary_test` 往返通过。

## 结论

RouteLifecycle 泛型收敛与 GLK1 字段类型安全化在干净冷启动下 Multicast 完整链路
PASS，可视为该批布局敏感变更的真机门禁通过。
