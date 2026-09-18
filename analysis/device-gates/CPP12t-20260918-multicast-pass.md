# CPP12t 真机门禁：Multicast（CPP12s hardening）— PASS（第一次冷机）

- 构建：APK 285；native `6713fba0f2643dbf52ac985de8101880f99e6d56c371c4828c8ad60360e7634d`
  （相对 `395c5faf…` 唯一改动：`do_kernel5_fake_lock_route` 增加 stamp 失败短路，155→162 指令；
  其余 7 个攻击关键函数形状一致，仅 `.text` 位移）
- 入口：Direct（干净冷启动，KernelSU 未加载；`boot_ms=33897`，攻击前 uptime ~34s）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12t-20260918-multicast-pass.native.log`（193 行）、`.ksu.log`（16 行）
- 结论：**PASS（第一次冷机）**；hardening 构建未复现 `CPP12s` 的 W1 panic

## 覆盖点

- **6/6 route** `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`（`route_done` 6/6、`threads joined` 6/6）。
- 所有 6 次 `prepare_kernel_page ok attempt=1`（2.2–3.4s），无页验收重试——对照 `CPP12s` 的
  `attempt=3 +6644ms` 与 `CPP12p/r` 的 3.3–3.5s，本次环境处于正常/良好窗口。
- W1 / W1b scratch repair / W2 / W2b prebuilt repair / W3 TIF_SECCOMP / W3 seccomp mode 全链；
  `Write 1 complete` T+10630ms、`exploit complete` T+32739ms。
- `child seccomp filter bypassed`、`enforce=1 (enforcing)`、`KernelSU ready`；
  KSU 日志 `policy fixup rc=0`、`late-load exit=0`、`KernelSU module loaded`。
- 零 warning/retry/failed/dirty/fallback；`multicast stamp failed` 未出现（成功路径不触发 hardening 分支）。
- `mcast ghost disarm ret=-1 errno=110`（预期 disarm 路径）。

## 判定

- hardening（stamp 失败不触发 PI walk）未改变成功路径行为，攻击全链完成；
- `do_kernel5_fake_lock_route` +7 指令的布局变化未引发 `KERNEL-PANIC-01`（第一次冷机）；
- 按布局敏感规则需第二次冷机（reboot 回干净启动），或由用户豁免。

## 后续

- 第二次冷机验证（或用户豁免）。
- 若两次冷机 PASS：`CPP12s` 候选 1（stamp 失败后仍触发 walk）已封堵；候选 2（stamp 落地后、
  walk 前被中断/栈覆盖）仍为原语残余风险，随后续门禁观察。
- 之后恢复既定计划：分层/namespace 小步重试与 pid 所有权重试（各自独立门禁）。
