# NSFUNC 真机门禁：命名空间收拢 + target.h 宏函数化（Multicast）— PASS

对应提交 `bef7c1c`（`refactor(native): functionize the target.h compile-time macros`），
以及此前同一轮的命名空间收拢系列（`b179f1a`…`4878d4f`）。

## 设备与入口

- 设备：`A301SO`（`adb -s QV770MFGJ1`），kernel `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`。
- 入口：direct（`--load-prebuilt-profile`，uid=2000、`u:r:shell:s0`、`Seccomp=0`），
  home `/data/local/tmp/ghostlock-app`。
- 路线：Multicast（one-shot `do_kernel5_fake_lock_route`）。
- CPU：`cpu pair: main=0 consumer=1`（profile `recommended_cpus` 与运行期 `selected_cpus` 均为 0/1）。
- 设备状态：冷启动后 `grep -i kernelsu /proc/modules` 为空（KernelSU 未加载），`uptime` 连续、无重启。

## 结果

- 7 次 `route_done` 全部 `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`
  （W1、W1 scratch repair、W2、W2b、W3 链、handoff 前的写入）。
- `[route] CMP_REQUEUE_PI ret=-1 errno=35`（EDEADLK，预期路径）→ `waiting route_done` → `route_done status=0`。
- `mcast ghost disarm ret=-1 errno=110`（ETIMEDOUT，预期 disarm 路径）。
- `child is root!`、`exploit complete`、handoff `KernelSU ready`。
- **无 kernel panic**，`uptime` 全程连续。

日志：`NSFUNC-20260922-multicast-pass.native.log`。

## 归因澄清（重要）

本轮期间出现过一次 `wait route_done` panic（`20260922-1102xx`，shizuku），经排查**与代码无关**：

1. `bef7c1c` 与 `40a03ef` 的 native 产物**逐字节完全相同**（`.text` 与整文件 `cmp` 一致，
   464 个函数大小零差异）——宏函数化未改变任何代码。
2. 同一 native 二进制：10:46 direct **PASS**（并加载了 KernelSU），11:02 shizuku 在
   **KernelSU 已加载**的设备状态下 panic。这正是文档记录的 re-enforce 崩溃分支
   （见 `CPP12-victim-pid-kernelsu-crash`）。
3. panic 会把 native 日志尾部从 page cache 丢掉（`CPP12s` 已记录），因此屏幕停在
   `waiting route_done` 而文件里看不到 route 行；本轮已把 11:02 前后目录一并核对。

结论：门禁必须在 **KernelSU 未加载的干净冷启动**下进行；清理冷启动后本页证据 PASS。

## 结论

命名空间收拢（`ghostlock::profile/config/session/route/memory/attack/kernel/support/legacy`
及全局/命名空间内兼容别名删除）与 `target.h` 88 个宏函数化，在干净冷启动下 Multicast
完整链路 PASS，可视为该轮布局敏感变更的真机门禁通过。
