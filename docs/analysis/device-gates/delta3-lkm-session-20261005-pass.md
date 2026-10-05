# δ-3 驻留语义修正 + 链内 LKM 窗口 真机门禁 — **PASS**（2026-10-05）

覆盖维护者裁决：**驻留生命周期绑定客户端会话（fd），不是定时器**；插件调用同步完成后立即 UNLOAD。

## 1. 直接内核侧测试（`tools/glkctl`，root 手动 insmod）

| 场景 | 证据 |
|---|---|
| **显式快速路径** | `glkctl seq`：`ping rc=0 → read rc=0 → write rc=0 → unload rc=0`；dmesg `resident window closed (calls=3 reason=explicit)` |
| **崩溃路径（核心）** | 持 fd 的进程 `kill -9` → **2 秒内** `/dev/glk` 消失、`ghostlock=0`、dmesg `reason=fd-close`（远早于 60s watchdog） |
| **泄漏兜底** | 无人打开/关闭时 dmesg `reason=watchdog`，模块自行卸载（仅病态情形） |
| 版本不匹配 | `ping abi=2 → rc=-71 (-EPROTO)`，fail-closed |
| 地址越界 | `-EFAULT`（地址合法性独立于「已授权」） |

## 2. 链内 43284 生产门禁（adb 免 App 试验台）

```
EXIT=0
lkm_window opened=1 closed=1 calls=0 abi_version=1 unload_ok=1
ghostlock=0                 # 模块已卸载
/dev/glk 不存在             # 设备节点随窗口消失
kernelsu=1                  # 攻击目标达成
Enforcing                   # SELinux 已恢复
dmesg: ghostlock: resident window closed (calls=1 reason=explicit)
```

AVB：`ok=12 fail=0 skip=0 err=0 sig_ok=5` → **`AVB_VERIFY=OK`**。

## 3. 本轮发现与修复（全部影响真机可用性）

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| ① | 客户端（shell 域，uid 2000）`open /dev/glk: Permission denied` | Android 的 `/dev` 由 **ueventd** 建节点，**忽略 `miscdevice.mode`** → 恒为 `0600 root` | 改为**先 `misc_register` 再跑 UMH**，让 root 脚本（SELinux 仍 permissive）`chmod 666` |
| ② | mode 修好后仍 `Permission denied` | 节点带通用 `u:object_r:device:s0`，shell 域无 `chr_file` 权限（`/dev/glk` 不在 vendor `file_contexts`） | 脚本 `chcon u:object_r:null_device:s0`（shell 域可 read/write/ioctl 的 chr_file 类型），**不必放宽 SELinux** |
| ③ | 链的 `run.wait outcome=Timeout`（标记已生成却看不到） | 终点等待上限 5s 与 UMH 脚本的 **ksud 发现**（扫描 `/data/app`）赛跑，冷缓存会擦过边界 | `wait_timeout_ms` 5s → **15s**（生产与分阶段两处） |
| ④ | 复跑时「脚本没跑、标记没有」 | 上一轮残留的**常驻模块**导致同名 `insmod` 失败（污染） | 门禁前必须清场：`UNLOAD`/`rmmod` + 清标记（已写入本门禁步骤） |

## 4. 生命周期（最终形态）

```
insmod(resident=1) → misc_register(预 UMH) → UMH 脚本(chmod/chcon) → permissive→enforcing 恢复
   → 常驻窗口开始（wait_event on released）
        ← native 开 fd、PING、同步执行插件（run_lkm_window）
   → native UNLOAD + close(fd)  ⇒ reason=explicit（正常路径，亚秒级）
   → 进程崩溃 ⇒ 内核关 fd       ⇒ reason=fd-close（兜底，已实测 2s 内）
   → fd 泄漏且永不关闭          ⇒ reason=watchdog（GLK_LKM_WATCHDOG_MS=60000，仅病态兜底）
   → misc_deregister → module_init 返回 -E2BIG → 内核卸载
```

## 5. 未覆盖（如实记录）

- **App 路径的标签**：`untrusted_app` 对 `null_device` 是否允许 `ioctl` **未验证**；App 驱动的运行若被拒，需要单独的标签决策
  （已在 `root_cmd.sh` 里留 NOTE，并记入 `contract-design.md`）。本次门禁走的是 shell 域（adb/Shizuku 路径）。
- 插件消费槽 `run_lkm_window` 目前为空实现（`calls=0`）；真实插件接入后 `calls` 应随之增长。

## 6. 复现

```sh
# 清场（必须：否则同名 insmod 失败）
su -c "/data/local/tmp/glkctl unload; sleep 1; rmmod ghostlock; sleep 1; \
  rm -f /dev/df /dev/dfm0 /dev/dfm1 /data/local/tmp/.ghostlock_lkm_ok /data/local/tmp/.ghostlock_lkm_fail"
su -c 'chmod 755 /data/local/tmp/.ghostlock_lkm_cmd.sh'
# 建 SA + encap socket（见 43284-production-adb-harness.md §5），然后：
adb shell 'cd /data/local/tmp && GHOSTLOCK_HOME=/data/local/tmp TMPDIR=/data/local/tmp \
  ./glk --ghostlock-app-call --enable-status-record < /data/local/tmp/appcall43284.bin'
```
