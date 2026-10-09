# 真机事实记录 · 旧版发布在设备取得 root + 模块试载（2026-10-08 · **部分结果 · 未归因**）

> 分析类（≤8192 B）。**只记事实与物证**；**不归因**（见 §5）。

## 1. 设备

| 项 | 值 |
|---|---|
| 设备 | **`A301SO`** · Android 15 |
| 内核 | `5.15.189-android13-8-00016-g51bba4309aac-ab14546557` |
| KernelSU | **已装且有管理器** ✓（`me.weishu.kernelsu`） |
| 无特权 shell | **无 root** ✓ |

## 2. 被测物（身份）

| 件 | 大小 | 身份 |
|---|---|---|
| GitHub 发布 `YuKongA/ghostlock-app` pre-release（2026-09-17）资产 **`ghostlock`** | **240 696 B** | sha256 **`4197e2618dbfe12f1614fdf6f933b5adec59a2e82dd3add5ec2306ddf494a95e`**（**与官方 `sha256sum.txt` 逐字节一致** ✓） |
| 档案 `GhostLock-kernel-profiles.zip` 内本机档 | **1917 B** | `5.15.189-android13-8-00016-g51bba4309aac-ab14546557.bin` |
| APK 内 `lib/arm64-v8a/libghostlock.so` | **240 696 B** | 与资产 `ghostlock` **同大小 ⇒ 同一二进制** ✓ |

**命令**：`GHOSTLOCK_HOME=/data/local/tmp/glp ./ghostlock-old --load-prebuilt-profile /data/local/tmp/glp/5.15.189-….bin --dump-kernel-log /sdcard/Download/gl-log`

## 3. 结果 ① **取到 root**（物证三条 + 原样日志）

```
[*] root script start uid=0 euid=0
[*] seccomp=Seccomp: 0 Seccomp_filters: 0
[*] ksud=/data/app/~~snip==/me.weishu.kernelsu-snip==/lib/arm64/libksud.so
[*] policyload before=0 after=2
[*] policy fixup rc=0
[*] late-load kmi=android13-5.15
[*] late-load exit=0
[+] KernelSU module loaded
```

- **同轮内核日志**：`ghostlock: /dev/glk registered (pre-UMH)` · `selinux_state set permissive` · `umh exec returned 0 for cmd=…/.ghostlock_lkm_cmd.sh` · `restored to enforcing` · `/dev/glk resident`；
- **`cat /proc/modules` 首行** = `kernelsu 204800 0 - Live … (OE)`；
- **`su -c id`** ⇒ `uid=0(root) context=u:r:ksu:s0` ✓。

## 4. 交叉验证（**对本仓新树有用**）

- 旧二进制自报 **cpu pair `main=0 consumer=1`** ✓ —— 与**新树从设备文档读出**的 race 参数 **一致** ✓：`main=0` · `consumer=1` · `settle=50000` · `poll=1000` · `rounds=300000`。

## 5. 结果 ② **模块试载（部分）** + **明示不归因**

- 以 root `insmod` 本机 label 的 `build/app/lkm/android13-5.15/ghostlock.ko` ⇒ **`insmod: failed to load …: Argument list too long（E2BIG）`**；
- **含义（2026-10-08 更正）**：**E2BIG = 该模块 `module_init` 的故意返回**（`tools/lkm/ghostlock/ghostlock.c:40-41`「Returning -E2BIG is deliberate: a failing module_init makes the kernel unload the module, so nothing stays resident」；`:460` = `return -E2BIG;`）⇒ 该次 **「已加载 → 已执行 → 已自卸载」**（**不是被拒载** ✗；未出现 `invalid module format`/`version magic`/`Unknown symbol`/CRC ✓）；
- **⚠ 非无害提醒**：默认参数下 init 会**改 SELinux 状态**（`permissive` 默认 **1**，`ghostlock.c:104`）且在 **`resident=1`** 下**常驻等待 UNLOAD/超时** ⇒ **一次裸 `insmod` 不是空操作**；**惰性变体** = `insmod ghostlock.ko permissive=0 resident=0 restore_enforce=0 cmd=/system/bin/true`（源码 `:30-33` 说明 `resident=0` 等价旧「跑完即自卸载」）；**是否再试由用户决定** ✓；
- **三类串一条未命中** ✗：**不是** `invalid module format / version magic`，**不是** `Unknown symbol` / CRC；
- 随后对照尝试**未取到输出** ⇒ **设备此时重启** ✓；**重启后**：root 消失 ✓（内存态利用不跨重启 ⇒ 预期）、**kernelsu 未加载** ✗、**他人留在 `/data/local/tmp` 的文件原样未动** ✓。

**未归因（明示）**：那次重启的成因**未知** ✗ —— **可能是本次试载，也可能是攻击余波**；**未再重试试载** ✓。

## 6. 判据边界（**务必如实**）

- 本条**不证明**本仓**重写树**的攻击可用 ✗；
- **探测语义（更正）**：本仓探测的 `belongs=no`（`main.cpp:175-182` 的 `vermagic_belongs_to` **文本比较**）是**保守判据**，**不等于内核会拒载** ✓（实测同 label 模块可加载可执行）；探测语义是否细化由 **native 侧**决定，**非本记录结论** ✓。
- 它证明的三件事：① **旧发布在设备上确实能取得 root** ✓（可复现，命令见 §2）② **本机 label 的模块可加载并执行** ✓（E2BIG = 模块自身 `module_init` 故意返回 ⇒ 已加载→已执行→已自卸载）③ **新树读出的运行时值与旧版一致** ✓（§4）。

## 7. 新事实（同轮只读）· `kptr_restrict = 2`

**命令与输出（原样）**：

```
# su -c "cat /proc/sys/kernel/kptr_restrict"
2
# su -c "grep -E ' (init_cred|selinux_state|init_nsproxy|init_task)$' /proc/kallsyms"
0000000000000000 T init_task
0000000000000000 D init_cred
0000000000000000 D selinux_state
0000000000000000 D init_nsproxy
```

- **事实**：`kptr_restrict` = **2** ✓；**以 root 读 `/proc/kallsyms` 的四个符号地址全为 `0000000000000000`** ✗ ⇒ **本设备上连 root 也无法从 kallsyms 取得符号地址** ✓。
- **含义（如实，不推断）**：档案中的**预置偏移**（`offset.*`）**因此不可用 kallsyms 现场核对** ✓；这也解释了**地址发现路线**（kernelsnitch 等）为何存在于设计 ✓。
- **未验证**：这些**预置偏移本身是否正确** ✗ —— 本条只证明「**无法用此法核对**」✓。
