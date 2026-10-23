# 2026-10-21 冷机 43499 standalone 门禁（FAIL）

## 设备

```text
model: A301SO (Sony Xperia 1 V)
serial: QV770MFGJ1
kernel: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
Android: 15
verified boot: green
page size: 4096
```

冷机：`adb reboot` 后 `sys.boot_completed=1`，`/proc/uptime=24.19s` 开跑；
门禁结束时设备仍在线，uptime `70.95s`，未 panic/重启。

## 入口

当前仓库 APK 不含 `libghostlock43499.so`，且 `:app:compileDebugKotlin` 仍被既有
缺失类阻断。因此本次用源码编译 standalone：

```text
aarch64-linux-android24-clang -std=gnu11 -O2 -fPIE -pie \
  -DTARGET_CONFIG_H="target.h" \
  -I app/src/main/cpp/glk43499 -I app/src/main/cpp/glk43499/kernels \
  -I app/src/main/cpp/glkcommon -I app/src/main/cpp/glk_log \
  main.c fops.c util.c offsets_json.c glk_session.c glk_log.c glk43499_api.c \
  -llog -o /tmp/glk43499
```

`adb push /tmp/glk43499 /data/local/tmp/glk43499`，shell 执行。

## 结果

`adb shell /data/local/tmp/glk43499` 返回 **rc=1**。

关键输出：

```text
offsets matched: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
soc: qcom/other; kernel_phys_load=0xa8000000
init_cred image=ffffffc082bfd588 alias=ffffff802abfd588
startup context pid=13138 uid=2000 euid=2000 gid=2000 egid=2000
  attr=u:r:shell:s0 enforce=1
startup limits NoNewPrivs=0 Seccomp=0 Seccomp_filters=0
build config label=ghostlock_oplus slide=pselect main=pselect

W1: SELinux attempt 1/1
[spray] futex collisions found +2399ms
[spray] mm_struct leaked=0xffffffffffffffff +4933ms
[-] KernelSnitch mm_struct leak failed
prepare_kernel_page retry 1/4
...
prepare_kernel_page retry 4/4
[spray] mm_struct leaked=0xffffffffffffffff +3572ms
[-] KernelSnitch mm_struct leak failed
[-] heap spray failed
[-] W1: SELinux attempt 1 route failed; backing off
[-] Write 1 failed
rc=1
```

## 判读

- 失败点不是 W1 写入，而是 **KernelSnitch mm_struct leak**；4 次重试全部 `-1`。
- 设备未重启/panic，cold boot 条件成立。
- 该 profile 标记 `requires_shizuku=1`；standalone shell 入口可能不具备
  Shizuku/app 域的 perf 权限。未取得 `perf_event_open`/SELinux 直接证据，
  不能把原因定死。
- 本次不是官方 APK/JNI 门禁，不能据此判断 app 路径 PASS/FAIL。

## 结论

**FAIL（standalone shell cold run）**；不得标 supported。

## 下一步

1. 修复 Kotlin 缺失类，产出含当前 `libghostlock43499.so` 的 APK。
2. 走 `NativeExploitBridge` + app 域跑同一冷机门禁。
3. 若仍 leak 失败，抓 `perf_event_open` 返回码/SELinux avc，再决定是否 profile 问题。

## 后续

KernelSnitch 已按 origin/main C++ 版修复，复跑进入 W1：
[20261021-1948-cold-43499-ksfix.md](20261021-1948-cold-43499-ksfix.md)。

## 原始输出

本地文件：`/tmp/glk43499-cold-gate.log`（未入库）。
