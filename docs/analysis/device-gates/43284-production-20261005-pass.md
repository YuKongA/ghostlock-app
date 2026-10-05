# 43284 **生产路径**（app-call → Pipeline）真机门禁 — **PASS**（2026-10-05）

驱动方式：**adb-only 试验台**（合成 IpSec SA + 手工会话帧），**不需要 App、不需要解锁屏幕**；
试验台细节与三个发现见 `43284-production-adb-harness.md`。

## 构建 / 设备

- 构建：工作树（含 `5b3fb29` 三处修复：hook guard Skip、shell 可读终点标记、脚本 +x）。
- 设备：A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，slot `_a`。
- 前置：XFRM transport ESP-in-UDP SA（`spi 0x4a1f0011`，AES-CBC/HMAC-SHA256，`encap espinudp 4500 4500`）+ `tools/espencap` 持有的 UDP 封装 socket（对应 `IpSecManager.openUdpEncapsulationSocket()`）。
- 入口：`--ghostlock-app-call --enable-status-record`，stdin = `[doc][84B 会话帧][预填 ACK]`；文档取 App 写出的 43284 `profile.bin`（`backend=cve_2026_43284`、`steps=3`）。

## 结果

```
$ GHOSTLOCK_HOME=/data/local/tmp TMPDIR=/data/local/tmp \
    /data/local/tmp/glk-fix2 --ghostlock-app-call --enable-status-record < appcall43284.bin
EXIT=0                                   # 生产 Pipeline 全链成功
$ ls -l /data/local/tmp/.ghostlock_lkm_ok
-rw-r--r-- 1 root root 0 ...             # 由 LKM 的 UMH 脚本创建（新标记）
$ tail -1 /data/local/tmp/.ghostlock_lkm.log
[+] KernelSU already loaded
```

**内核日志（dmesg）链路证据**

```
KernelSU: hook_manager: unmark 9552 exec /vendor/bin/insmod   ← 双重 fork 命中 libc++ hook → shellcode → insmod
ghostlock: selinux_state set permissive                       ← 我们的 LKM 已加载
ghostlock: umh exec returned 0  for cmd=/data/local/tmp/.ghostlock_lkm_cmd.sh
ghostlock: selinux_state restored to enforcing                ← 收尾恢复（自卸载）
```

（此前同一链路记录为 `umh exec returned 32256` = 退出码 126「脚本不可执行」——那是试验台 `adb push` 覆盖脚本时丢掉 `+x` 位所致，`chmod 755` 后为 0。）

## 事后核验

```
$ su -c id            →  uid=0(root) … context=u:r:ksu:s0
$ getenforce          →  Enforcing
$ grep -c kernelsu /proc/modules   →  1
$ grep -c ghostlock /proc/modules  →  0        （返回 -E2BIG 自卸载）
$ avbcheck avb-verify --slot _a    →  ok=12 fail=0 skip=0 err=0 sig_ok=5   AVB_VERIFY=OK
$ avb_guard.sh files-check         →  fail=2（恰为两处页缓存写，符合机制）
      [FAIL] /apex/com.android.runtime/bin/crash_dump64   ← patch #1
      [FAIL] /vendor/lib64/libbinderdebug.so              ← .ko 载体
      [ok]   /system/lib64/libc++.so                      ← hook 已 restore
```

## 门禁过程中修复的三个真实缺陷

| # | 缺陷 | 修复 |
|---|---|---|
| ① | 生产路径硬编码 `HookGuardPolicy::Reject`，本机 sentry 有 `bti c` 守卫 → `HookFailed(12)` | 改 `Skip`（`5b3fb29`；`TODO(S4)` 做成 profile 字段） |
| ② | 终点标记 `/dev/dfm0` 在 Enforcing 下对 shell 域 `getattr` 被拒 → 永远 `WaitTimeout(15)` | 新增 shell 可读镜像标记 `.ghostlock_lkm_ok/_fail`，脚本与判定同时接受（`5b3fb29`） |
| ③ | 试验台覆盖脚本丢 `+x` → `umh exec returned 126`，无标记 → 超时 | `chmod 755`（试验台要求，已写入 `43284-production-adb-harness.md`） |

**重要澄清**：修复 ② 之前，staged 运行报的 `outcome=LkmLoaded` 是**陈旧 `/dev/dfm0` 的误判**；
从而一度掩盖了 ③。修复后两条路径（staged 与生产）判定一致，且本轮为**生产路径**真实通过。

## 复现

见 `43284-production-adb-harness.md` §5（建 SA/socket → 推 native+helper.ko+文档/帧 → 跑）；
每次运行前需 `rm -f /dev/df /dev/dfm0` 并保证脚本 `chmod 755`。

## 未覆盖（如实记录）

- 本门禁走的是**试验台**（合成 SA），不是 App 的 `IpSecManager`；App 真实路径应在下一轮用 App 复跑（同一 Pipeline / 同一链）。
- XFRM SA 与 encap socket 不跨重启，试验台需重建；不影响生产代码。
