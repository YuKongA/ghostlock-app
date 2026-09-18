# CPP08 Direct 门禁失败：攻击完成但 KernelSU 未加载

- APK：`versionCode=204`（提交 `f333a22`），native `8e5cd481…`
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Direct（Shizuku 开关关闭）；CPU main=0、consumer=1
- 温度：日志不可判定
- 结论：失败（攻击链成功，KernelSU 交接未生效）

## 日志事实

- W1 经历 3 次页面验收重试后 `SELinux permissive`；W1b、W2、W2b、W3 全部成功；
- 6 次 route 全部 `route_done status=0 clean=1/1`，`child is root!`、`child seccomp filter bypassed`；
- `[T+34925ms] exploit complete` 后 native 打印
  `temporary root ready; KernelSU module load pending`（ksu log 30s 轮询无结果）；
- App 私有目录中 `.ghostlock_root.sh` 存在（4726 字节、0755、`HOME_DIR` 正确），
  但 `.ghostlock_ksu.log` 从未生成；
- 设备 `/proc/modules` 无 `kernelsu`。

## 已排查项

- 手动以 App UID 执行同一脚本：可正常写入 `.ghostlock_ksu.log`（343 字节）并在非 root 时按预期 `[!] temp su unavailable; aborting` 退出 —— 脚本本身、路径与目录写权限均正常；
- 残留进程：victim（`ghostleaf_01234`，root）park 中，其一个 zombie 子进程 exit code 为 0；
- 环境：permissive（W1 生效后未恢复 enforcing），排除了 SELinux 阻止脚本写日志。

## 待定位

native 侧 handoff（`child_main` 的 'G' 分支 → worker `execl("/system/bin/sh", script)`）是否真正执行、
以及 child 中 `g_root_script_path`（`RuntimeConfig` 的 `std::string` 经 `.c_str()`）的实际取值与 `execl` 结果，
现有日志均无法区分。本次提交在 handoff 边界加入诊断输出（不改变控制流）：

- `root script written path=… bytes=…`
- `handoff: child=… alive=… sent=… errno=…`
- `handoff: root script path=…`（child 侧）
- `handoff: script open fd=… errno=…` 与 `execl root script failed …`（worker 侧）

复测后按上述输出定位根因。

原始日志：[`CPP08-20260917-direct-kernelsu-pending.native.log`](CPP08-20260917-direct-kernelsu-pending.native.log)
