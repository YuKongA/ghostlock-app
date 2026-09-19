# CPP12 VictimContext pid 复现：KernelSU 环境下系统崩溃（dmesg 捕获成功）

- 构建：APK 264；native `b147df9f725873cbcf3fad3d7c68544fe3a6963acd609c72882f76be5950e6ec`（与 `CPP12k` 的 pid 所有权构建逐字节相同）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 捕获：`CPP12-victim-pid-kernelsu-crash-dmesg.log`（3531 行；root 下 `dmesg -w` 实时落盘，崩溃瞬间在 `680.205227` 截断）
- 结论：**本次崩溃的主导变量是运行环境（KernelSU 已加载），不是 pid 构建本身**

## 环境声明（本次与历史门禁的关键差异）

- 为获取 adb root，用户在本 boot 内**启动了 KernelSU**（`/data/adb/ksu` 生效，`su` 可用，`scontext=u:r:ksu:s0`）。
- 历史 Multicast 门禁（`CPP12m/n`、`CPP14-release` 等）均在 **KernelSU 未加载**的干净启动上运行；攻击自身会在成功后加载 KernelSU。
- 日志证据（本 boot）：
  - `KernelSU: tp_marker: unmark process: ... comm:m.ghostlock.app` / `comm:libghostlock.so`（557s，攻击进程被 KernelSU 处理）
  - `audit: ... pid=22570 comm="ghostleaf_01234" ... scontext=u:r:kernel:s0 ... permissive=1`（victim 已以 kernel 域写日志 = W2 已成功）

## 崩溃时间线（dmesg，uptime 秒）

| 时间 | 事件 |
|---|---|
| 557s | KernelSU 已加载并处理 GhostLock 进程；victim `ghostleaf_01234` 以 `kernel` 域活动（W2 成功） |
| 632s | 首批 `binder: undelivered transaction ... process died`（进程开始死亡） |
| 667–677s | `SELinux: avc: denied { find } ... scontext=u:r:system_server:s0 ... permissive=0` 连续出现（**SELinux 被 KernelSU re-enforce 回 enforcing**） |
| 678–680s | 大量进程死亡（binder death notification/transaction 批量 undelivered）、`gunyah: Failed to dealloc VMID`、`CAM_WARN ... userspace process has died` |
| 680.205s | 日志在未完成行处截断 → 系统重启 |

- **无 `Kernel panic` / `BUG:` / `Call trace` / `Unable to handle` 打印**：不是传统内核 panic；更符合“系统级服务崩溃/内核挂起后看门狗复位”。

## 归因

1. KernelSU 已加载时，攻击写 `init_cred`/切换 SELinux 的状态与 KernelSU 的内部管理（re-enforce、hook）冲突：SELinux 在攻击中途被 KernelSU 恢复为 enforcing，system_server 连续权限异常并崩溃，最终系统重启。
2. 本次崩溃位置（W2 成功之后）与 `CPP12k` 的两次（W2 spray 窗口）不同，但都属于“攻击进行中的系统级崩溃”；`CPP12k` 时设备是否也处于 KernelSU 已加载状态**日志不可判定**（无内核日志）。
3. 该证据将 `KERNEL-PANIC-01` 的诊断推进为：**存在“环境相关”的崩溃分支（KernelSU 已加载）**，与“构建布局敏感”假说并列。

## 行动

- **门禁环境规则（新增）**：Multicast 门禁前必须确认 `cat /proc/modules | grep kernelsu` 为空（干净启动），不得为获取 root 而主动加载 KernelSU；`dmesg -w` 捕获需要 root，仅在诊断会话中使用，且该会话的门禁结果要标注“KernelSU 已加载”。
- 在干净环境（当前 boot 未加载 KernelSU）复跑 APK 264，判定 `CPP12k` 的两次崩溃是否仍可复现。
