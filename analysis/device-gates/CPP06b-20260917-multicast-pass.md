# CPP06b Multicast 真机复测（CPP02/03/06 收尾后）

- APK：`versionCode=196`，构建时间 `2026-09-17 20:10:38`
- 提交：`e980bd5`（CPP02/03/06 收尾）
- native SHA-256：`b62459555518039b8be7868d0d785dae914085dd1e05e814d2b7e13bdbf38090`
  （设备 `base.apk` 内 `lib/arm64-v8a/libghostlock.so` 与本机构建产物一致）
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557（qcom/other）
- 入口：Direct；CPU main=0、consumer=1
- 温度：日志不可判定
- 结论：通过

## 验证要点

- 6 次 route 全部 `multicast route status=0 clean=1/1 step=0 errno=0`，`calls=1 success=1` 且完成 join；
- W1 首轮同样被 `selinux_state.initialized` 页面验收拒绝，第 2 次通过；W1b、W2、W2b、W3 全部成功；
- KernelSnitch：collision `1970–2333ms`、leak `1998–2439ms`（CPP04-06 基线 `1928–2149ms` / `2000–2260ms`，处于噪声范围）；
- `child is root!`、`child seccomp filter bypassed (errno=22)`、`KernelSU ready`、`enforce=1`。

## 与基线的差异

- 收尾改动（`ms_since`/`read_first_line` RAII、真实 fork 失败注入测试、`KernelSnitchOwner`）未观察到行为或时序变化；
- `mcast ghost disarm ret=-1 errno=110` 与基线一致，不判定为回归；
- Resident Multicast 未启用，本日志不覆盖 resident 路径。

原始日志：[`CPP06b-20260917-multicast-pass.native.log`](CPP06b-20260917-multicast-pass.native.log)
