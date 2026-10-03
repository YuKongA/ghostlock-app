# A2-1 真机门禁：环境探测迁移到 platform::runtime（multicast_waiter，direct）— PASS

对应提交 `3e60c55`（Phase A2-1）。候选二进制 `build/native/ghostlock`
SHA-256 `96284cbbcf2f11846af538760382f180821e5df7231f92cd57883bf88f05c538` — **与 Phase A 整体门禁
`A3-01` 完全相同**，即本批纯源码组织迁移在二进制层面零差异（行为中性的直接证据）。
设备端 profile `p0.bin` SHA-256 `dac9e63132b40e7fcca362bddc1bb5e26c124df772d8e26b6e03b566a0997b72`（未修改）。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；`ro.build.version.incremental`
  `067002A003017800602211349`；冷启动 `boot_ms=32177`；KernelSU 未加载、Enforcing、uid 2000。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a2 --load-prebuilt-profile
  /data/local/tmp/p0.bin`；route = multicast_waiter；CPU 对 0/1。

## 结果

PASS（完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 sockopt=-1 errno=0 attempts=16 calls=1 success=1   # SELinux
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1               # W2
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1               # W2b x2
[+] child is root!
[*] [T+16816ms] exploit complete
[*] handoff: child=11281 alive=1 sent=1 errno=0
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root) context=u:r:ksu:s0`；无 panic。

## 变更说明

`check_selinux_off`/`enforce_readable`/`process_has_seccomp` 从 `attack::ops.hpp` 迁到
`platform::runtime`（`src/core/platform/runtime.hpp`）；host 数据流 harness 新增 `platform/runtime.hpp`
影集桩，脚本语义不变。`cmp_disasm --reviewed` 6 函数骨架一致。
