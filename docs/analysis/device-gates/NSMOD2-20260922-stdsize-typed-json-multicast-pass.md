# NSMOD2 真机门禁：std::size + legacy JSON 类型安全写入（Multicast）— PASS

对应提交 `834280d`（`refactor(native): std::size and type-safe legacy JSON field writes`）。

## 设备与入口

- 设备：`A301SO`（`adb -s QV770MFGJ1`），kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct（`--load-prebuilt-profile`，uid=2000、`u:r:shell:s0`、`Seccomp=0`），home `/data/local/tmp/ghostlock-app`。
- 路线：Multicast（one-shot）；`cpu pair: main=0 consumer=1`。
- 设备状态：冷启动、`kernelsu` 未加载；`uptime` 全程连续。

## 结果

- 4 次 `route_done status=0 clean=1/1 step=0 errno=0 calls=1 success=1`。
- `child is root!`、`exploit complete`、handoff `KernelSU ready`。
- **无 kernel panic**。

日志：`NSMOD2-20260922-stdsize-typed-json-multicast-pass.native.log`。

## 变更说明

- `sizeof(a)/sizeof(a[0])` → `std::size(a)`（`route_operations.cpp`、`profile_binary.cpp`、`offsets_json.cpp`）。
- `legacy_support/offsets_json.cpp`：删除 `store_profile_scalar` 与 `ScalarWidth`；`g_symbol_map`/`g_task_map`/`g_profile_map` 与 `execution_field` 改为按成员表达式生成的 typed store 函数指针，`offsetof`/`reinterpret_cast` 写入全部消除；截断语义由成员类型决定（与原 width switch 一致），host `offsets_json_test` 往返通过。
