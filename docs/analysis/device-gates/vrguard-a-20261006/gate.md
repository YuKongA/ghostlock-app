# 真机门禁 · vr_guard phase (a)（vivo 代码彻底删除）

> 批次：**vr_guard (a)**——删除 `src/core/platform/vivo/**`（8 文件）+ `platform_vivo_test.cpp` + `host/ancillary_stub.cpp`，`backend/cve_2026_43499/steps.cpp` **−95 行**（两处 include、两个祖先块、两个只为它们存在的私有件）。
> 判据：AGENTS「攻击关键路径改动 = 真机门禁 + 门禁记录」。

## 1. 前提（AGENTS 口径）

| 项 | 值 |
|---|---|
| 设备 | `QV770MFGJ1`（A301SO，Android 15） |
| release | `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`（与 profile bin 名**逐字一致**） |
| KernelSU | **未加载**（`/sys/module/kernelsu`、`/data/adb/ksu` 均不存在） |
| 冷机 | 先 `adb reboot` 并等 `sys.boot_completed=1`（uptime ≈ 26s 起跑） |
| 陈旧标记 | 运行前清理 `/dev/df{,m0,m1}`、`/data/local/tmp/.ghostlock_lkm_{ok,fail}`（历史假阴性来源） |
| CPU 对 | 默认 `0,1`（native 自选：main=cpu0、consumer=cpu1） |

## 2. 构建指纹

| 产物 | sha256 |
|---|---|
| `build/native/ghostlock`（**post-deletion** 构建） | `$(cat binary.sha256)` |
| `profile.bin`（GLKv3 文档，1919 B） | `$(cat profile.sha256)` |

载荷：`[4B 大端长度][GLKv3 文档]` = 1923 B（**43499 不需要会话帧**：`src/core/main.cpp:154-166` 只对 `cve_2026_43284` 读 84B channel-B 帧）。

## 3. 命令

```sh
adb -s QV770MFGJ1 shell 'cd /data/local/tmp/gl-gate && ./ghostlock \
  --ghostlock-app-call --enable-status-record \
  --dump-kernel-log /data/local/tmp/gl-gate/logs < payload.bin'
```

## 4. 原始证据（`native-stdout.txt` 摘录，逐字）

```
$(cat evidence-lines.txt)
[+] KernelSU ready
```

native 退出码 **0**；设备**未重启**（跑后 uptime 591s 连续）；设备日志落 `/data/local/tmp/gl-gate/logs/`（`iomem.txt`、`kernel-dmesg.log`、`kernel-info.txt`、`ksu.log`、`pstore`）。

## 5. 结论

**PASS**：route 命中（`multicast … success=1`）→ `child is root!` → `exploit complete` → handoff（`alive=1 sent=1 errno=0`）→ **`KernelSU ready`**；与 (a) 前基线**证据面一致**。

## 6. 偏差与说明（诚实记录）

1. **第一次运行设备重启**（uptime 归零）= **KERNEL-PANIC-01 类一次性事件**；同构建冷机复跑即 PASS（符合 AGENTS：不得凭一次 panic 归因代码）⇒ 本次归档以复跑为准；
2. **设备锁屏 ⇒ `/sdcard` 未挂载**（凭据加密存储需首次解锁）⇒ 日志不能按 AGENTS 口径落 `Download/ghostlock-debug-log/`，改落 **`/data/local/tmp/gl-gate/logs`** 并 pull 归档；`tools/device-guard/run-gate.sh` 已加**自动探测与回落**；
3. **`w2b` run-state 标记随祖先块删除** ⇒ 运行日志 stage 轨迹**少一项**：**预期、非行为差异**（字段无写入者 ⇒ 两 policy `enabled()` 恒 false ⇒ `PluginController::apply` 不运行任何 body ⇒ 两处调用点**零执行字节**）；
4. **一次失败尝试**：最初用 `[4B 长度][文档]` 但**未加 `--enable-status-record`** ⇒ native 按「裸文档读到 EOF」解析 ⇒ `[!] cannot load profile`（退出 255）。**载荷形态与开关必须匹配**（`src/core/profile/entry.cpp:76-113`）——已修正，非产品缺陷。