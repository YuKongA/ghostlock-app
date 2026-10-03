# A3-01 真机门禁：Phase A 机械重排整体回归（multicast_waiter，direct）— PASS

对应提交 `a43d5ee`（Phase A 批 3e 后，覆盖 A1 + 批 2 + 批 3a–3e）。候选二进制 `build/native/ghostlock`
SHA-256 `96284cbbcf2f11846af538760382f180821e5df7231f92cd57883bf88f05c538`。
设备端 profile `p0.bin` = 仓库导出 `5.15.189-android13-8-00016-g51bba4309aac-ab14546557.bin`，
SHA-256 `dac9e63132b40e7fcca362bddc1bb5e26c124df772d8e26b6e03b566a0997b72`（未修改）。

## 设备与入口

- 型号 A301SO；`uname -r` = `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；
  `ro.build.version.incremental` = `067002A003017800602211349`。
- 入口：CLI，`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a3 --load-prebuilt-profile
  /data/local/tmp/p0.bin`；route = multicast_waiter；CPU 对 0/1。
- 冷启动 `adb reboot` → `sys.boot_completed=1` 后运行，`boot_ms=32072`；KernelSU 未加载、Enforcing、
  `uid=2000 attr=u:r:shell:s0`。

## 结果

PASS（完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 sockopt=-1 errno=0 attempts=16 calls=1 success=1
[+] SELinux permissive
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # W2
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # W2b repair
[+] child is root!
[+] no app seccomp filter (adb/shell flow); skipping W3
[*] [T+16764ms] exploit complete
[*] handoff: child=11181 alive=1 sent=1 errno=0
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root) context=u:r:ksu:s0`；无 panic。

## 日志

- 本地：`docs/analysis/device-gates/A3-20261003-multicast-direct-pass.native.log`。

## 变更说明

Phase A 纯结构/命名搬迁的端到端回归：`frontend → terminal`；`Pipeline<Backend, Terminal>` + route 内部化 +
`terminal::RootedChild`；目录/命名空间 `pipeline/`、`backend/`、`backend/cve_2026_43499/route/`、`terminal/`、
`ancillary/`、`backend/victim/`、`memory/`。行为与语句/日志顺序不变；`cmp_disasm --reviewed` 全程 PASS。
