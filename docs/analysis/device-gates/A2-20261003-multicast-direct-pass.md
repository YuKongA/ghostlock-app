# A2-01 真机门禁：Phase A 批 2 接口重排（multicast_waiter，direct）— PASS

对应提交 `0ed1029`（`refactor(pipeline): Pipeline<Backend, Terminal> with route-internal dispatch
and RootedChild (Phase A batch 2)`）。候选二进制 `build/native/ghostlock` SHA-256
`baf71005691511258afcb052efc2d89c7253bc22f6118913cfec9f2b8b2cfcec`。
设备端 profile `p0.bin` = 仓库导出 `5.15.189-android13-8-00016-g51bba4309aac-ab14546557.bin`，
SHA-256 `dac9e63132b40e7fcca362bddc1bb5e26c124df772d8e26b6e03b566a0997b72`（未修改）。

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；
  `ro.build.version.incremental` = `067002A003017800602211349`。
- 入口：CLI，`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a2 --load-prebuilt-profile
  /data/local/tmp/p0.bin`；route = multicast_waiter（来自 profile）；CPU 对 0/1。
- 冷启动 `adb reboot` → `sys.boot_completed=1` 后运行，`boot_ms=34941`；KernelSU 未加载、SELinux
  Enforcing、`uid=2000 attr=u:r:shell:s0`。

## 结果

PASS（完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 sockopt=-1 errno=0 attempts=16 calls=1 success=1
[+] SELinux permissive
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # W2
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # W2b repair
[+] child is root!
[+] no app seccomp filter (adb/shell flow); skipping W3
[*] [T+16787ms] exploit complete
[*] handoff: child=11208 alive=1 sent=1 errno=0
[+] KernelSU ready
```

事后：`cat /proc/modules | grep -i kernelsu` → `kernelsu ... (OE)`；`su -c id` →
`uid=0(root) ... context=u:r:ksu:s0`；无 panic，uptime 正常。

## 日志

- 本地：`docs/analysis/device-gates/A2-20261003-multicast-direct-pass.native.log`。
- 设备：`/data/local/tmp/.ghostlock_ksu.log`、`/data/local/tmp/.ghostlock_root.log`。

## 变更说明

验证 Phase A 批 2 的行为无回归：

- `Pipeline<Backend, Terminal>` + `ComponentSelection={BackendKind,TerminalKind}`；route 由 backend 内部
  按 `decoded.route` 分发（`run_steps<Route>`）；terminal 输入改为中性 `terminal::RootedChild`，backend 在
  `Continue` 前转移 pid/command/uid_read/flags。
- 攻击函数：`cmp_disasm --reviewed` 6 函数骨架一致、仅全局数据位移，RESULT: PASS。
- 真机：multicast 写验证 `status=0 clean=1/1` ×3、`child is root!`、`KernelSU ready`，无 panic。
