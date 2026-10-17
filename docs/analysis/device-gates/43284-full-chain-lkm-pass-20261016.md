# 43284 真机门禁：全链 + 自建 LKM —— **PASS**（2026-10-16）

设备 A301SO（`QV770MFGJ1`）/ `5.15.189-android13` / Android 15；冷机后 KernelSU 未加载；同一会话内完成。

## 判据（全部命中）

| 判据 | 结果 | 真机证据 |
|---|---|---|
| `uid=0` | ✅ | `su -c id` ⇒ `uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0` |
| SELinux Enforcing | ✅ | `getenforce` ⇒ `Enforcing` |
| `/proc/modules` 含 kernelsu | ✅ | `kernelsu 217088 1 - Live 0xffffffd925b2b000 (OE)`（count=1） |
| 成功标记 | ✅ | `/dev/dfm0` 存在（`/dev/df` 亦在；`/dev/dfm1` 不存在） |
| UMH late-load | ✅ | `/data/local/tmp/.ghostlock_lkm.log` mtime **2026-10-16 20:46:43**；`late-load kmi=android13-5.15 … exit=0` · `[+] KernelSU module loaded` |

## 链内读数（应用耐久日志 `files/ghostlock-run.log`）

```text
hook payload slots=0 / payload wrote=true code=0
hook site block before=bf 23 03 d5 c0 03 5f d6 5f 24 03 d5 83 2d 01 14 word=0x14012d83
hook site block wrote=true afterWord=0x14012d83 verified=true
module bytes=12304 / module init=true code=0 / module wrote=true
```

写阶段：`patch#1`（1440 B / 90 块）· carrier（12304 B / **769 块**，逐块回读全过，等同退役 `written=769 verified=769`）· 进程序
号 91 接续（同一 SA 一条链）· ESP 组帧与上游逐项一致（24 字节头 / HMAC 覆盖 40 字节 / ICV 16 B / IV=ECB_DEC(旧)^期望）。

## 最后一块拼图（本会话根因）

`TypedBackendProbe.kt` 的 `fillPayloadSlots(...)` 调用点**显式**传了 `mutexPath = "/dev/dx"`，覆盖了默认值，
于是页里长期留旧串（真机 `dd` 逐字节可证：偏移 0xFE628 处为 `/dev/dx`）。
改为参考值 `/dev/df`（`hook_patch.hpp:146` 逐字）后，同一会话即跑通。

## 复核命令

```text
adb -s QV770MFGJ1 shell 'grep -i kernelsu /proc/modules'
adb -s QV770MFGJ1 shell 'ls -l /dev/df /dev/dfm0 /dev/dfm1'
adb -s QV770MFGJ1 shell 'tail -6 /data/local/tmp/.ghostlock_lkm.log'
adb -s QV770MFGJ1 shell 'su -c id; getenforce'
adb -s QV770MFGJ1 shell 'dd if=/system/lib64/libc++.so bs=1 skip=$((0xFE628)) count=8 | od -An -c -v'   # 应为 /dev/df
```
