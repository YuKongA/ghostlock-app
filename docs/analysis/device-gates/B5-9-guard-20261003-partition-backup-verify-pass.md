# 设备分区安全网：备份 + 冷机重启完整性复核（A301SO）— PASS

目的：在对 `/apex`、`/system`、`/vendor` 做任何页缓存写之前建立**分区级备份**，并在冷机重启后做**完整复核**，
确认「受保护分区不随重启变化」，作为后续 43284 写/触发门禁的回退基线。

## 工具

`tools/device-guard/backup_partitions.sh`（提交 `5130eb4`…`1848cdd`）：`backup` / `verify` / `restore <name>`；
经 `adb exec-out su -c dd` 读写块设备，`sha256` 清单 `manifest.tsv`。备份位于 `build/device-backup/QV770MFGJ1-20261003/`（22GB，未入库）。

## 备份范围（26 个分区）

- 物理/AVB：`super`、`misc`、`metadata`、`vbmeta_a/b`、`vbmeta_system_a/b`、`boot_a/b`、`init_boot_a/b`、`dtbo_a/b`、`vendor_boot_a/b`、`recovery_a/b`、`vm-bootsys_a/b`；
- 只读 verity 视图：`system-verity`、`system_ext-verity`、`product-verity`、`vendor-verity`、`odm-verity`、`system_dlkm-verity`、`vendor_dlkm-verity`。
- 设备：`ro.boot.verifiedbootstate=green`、`vbmeta.device_state=locked`、slot `_a`、A/B、动态分区。

## 冷机重启后的复核结果

| 分区 | 结果 |
|---|---|
| `super` | **[ok]**（逐字节一致） |
| `misc` | **[ok]** |
| `metadata` | **[FAIL]** 预期：可写状态分区，重启后 `615de842…` → `0a46eb5d…` |
| `vbmeta_a/b`、`vbmeta_system_a/b` | **[ok]** |
| `boot_a/b`、`init_boot_a/b`、`dtbo_a/b`、`vendor_boot_a/b` | **[ok]** |
| `recovery_a/b`、`vm-bootsys_a/b` | **[ok]** |
| `system-verity`、`system_ext-verity`、`product-verity`、`vendor-verity` | **[ok]** |
| `odm-verity`、`system_dlkm-verity`、`vendor_dlkm-verity` | **[ok]** |

**结论**：冷机重启前后，**所有 AVB 验证内容与只读镜像逐字节不变**；唯一变化是 `metadata`（状态分区，非 AVB 内容）。
因此「受保护分区跨重启不变」成立，复核基线可用。

## 过程记录

- 复核前：`adb reboot` → `boot_completed=1`（7 次轮询）→ `uptime 0 min`、`/proc/modules` 无 `kernelsu`、`su` 不存在、`verifiedbootstate=green`（**干净启动**）。
- `verify` 需要 root 读块设备；重启后 KernelSU 已卸载，故先用 **43499/GLKv3 路径**重新取 root（`--load-prebuilt-profile`，冷机一次成功，`KernelSU ready`、`su` uid=0）——顺带再次证明该路径在干净启动上可用。
- 复核工具修复三处（均已在同一提交内）：`set -e` 下 `&& continue` 提前退出、`pipefail` 静默中止、**内层 `adb` 继承 `while read` stdin 导致清单被读空**（`</dev/null`）。

## 回退（任意时刻）

```sh
bash tools/device-guard/backup_partitions.sh restore QV770MFGJ1 build/device-backup/QV770MFGJ1-20261003 super   # 或任意分区名
```
写回块设备需 root（KernelSU）；`super` 覆盖全部逻辑分区（system/vendor/product/system_ext/odm/dlkm）。
`metadata` 属状态分区，**不要**用备份覆盖（会破坏当前加密状态）。
