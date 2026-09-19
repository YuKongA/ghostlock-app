# CPP14 Release 验证：Multicast（Release APK）— 攻击中断（W1b spray 后）

- 构建：`GhostLock-v1.1(255)-arm64-v8a-release.apk`；native `625d5300f7312a44de802a3f48ab3757ce6d626c192c2109ed27c7280d654252`（与 `CPP12m`/`CPP12n` Debug 门禁版**逐字节相同**）
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`
- 日志：`CPP14-release-20260917-multicast-crash.native.log`（52 行，运行中被截断；release APK 不可 `run-as`，复装同签名 debug APK 后从私有目录导出）
- 结论：**构建链与启动协议 PASS；攻击未完成（W1b spray 后设备重启）**

## Release 构建验证（PASS）

- `assembleRelease` 成功：R8 压缩、`tools:ignore="Instantiatable"` 修复 Shizuku UserService 的 lint 误报（`0a55729`）。
- native 依赖：仅 `libm.so`/`libdl.so`/`libc.so`（静态 libc++，无 `libc++_shared.so`），DYN/PIE、AArch64。
- APK 4,955,442 字节（Debug 为 ~65 MB）。
- 启动协议：native 正常启动、`resolved profile loaded`、CPU main=0/consumer=1、`runtime verbose_debug=0`（Release 无 Debug execution 快照，符合设计）、W1 route `OK clean=1/1`、`SELinux permissive`——Kotlin/R8 侧未破坏参数、profile 与日志路径。

## 攻击中断

- 截断点：W1b `[T+9231ms] heap spray done` 之后、W1b route 之前（日志尾部为未 flush 的空洞）。
- 设备在运行后重启（复装 debug APK 时 `uptime` 仅 38 秒），与 `KERNEL-PANIC-01` 的 spray 窗口模式一致（`CPP12b/c/e`、`CPP12k`）。
- native 与 `CPP12m/n` 两次冷机通过版本逐字节相同，因此不归因于 Release 打包（R8/Kotlin）或本阶段 native 改动；判为攻击层间歇性内核崩溃（`KERNEL-PANIC-01`）。
- 温度与冷却间隔：日志不可判定。

## 后续与复跑结果

- Release 冷机复跑 **PASS**：`CPP14-release-20260917-multicast-pass`（8/8 route、`KernelSU ready`、T+41610ms）。
- 结论：首次失败为攻击层间歇性（native 与 Debug 门禁版逐字节相同），Release 打包与启动协议无回归。
- `KERNEL-PANIC-01` 追加本次证据（Release 环境、native `625d5300`、W1b spray 窗口 1 次；同构建复跑通过）。
