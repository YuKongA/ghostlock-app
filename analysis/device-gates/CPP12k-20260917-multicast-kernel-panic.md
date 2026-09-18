# CPP12k 真机门禁：Multicast（VictimContext 收编）— kernel panic ×2

- 构建：APK 242；native `b147df9f725873cbcf3fad3d7c68544fe3a6963acd609c72882f76be5950e6ec`
- 入口：Direct；`enforce=unreadable`；CPU main=0 consumer=1
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 日志：`CPP12k-20260917-multicast-kernel-panic-a.native.log`（119 行，23:09:39 运行）、`CPP12k-20260917-multicast-kernel-panic-b.native.log`（95 行，23:11:21 运行）
- 结论：**两次连续 kernel panic**；同构建未取得任何通过运行

## 两次截断点

| 运行 | 最后日志 | 阶段 |
|---|---|---|
| a（23:09:39） | `[spray] mm spray + kernelsnitch ready (cpu=8) +333ms` | W2 attempt 1 的第二次 spray（碰撞搜索开始前） |
| b（23:11:21） | `[T+12394ms] heap spray done` | W2 attempt 1 的 spray 完成后、fast-repair/route 之前 |

两次都发生在 W2 的 heap spray 窗口；W1 与 W1b 在该构建上曾成功执行（a 运行到 `Write 1 complete`、`private scratch repaired`）。

## 与稳定基线（`cea1bedc` / CPP12j PASS）的二进制对比

- 8/8 攻击关键函数（`waiter_thread`、`owner_thread`、`consumer_thread`、`do_one_write`、`do_kernel5_fake_lock_route`、`run_main_route_threads`、`multicast_waiter_worker`、`multicast_owner_worker`）**严格逐指令一致**。
- 640 个共有函数中 639 个指令形状一致；唯一形状差异是 `run_exploit`（+5 指令，victim 对象构造/析构与 pid 管理）。spray 路径（`prepare_kernel_page`、`prepare_ctxs`、`cleanup_page_prepare_state`、KernelSnitch 各函数、`clone_child`/`open_memfd`/`kill_child`）形状一致。
- 差异集中在 `.text` 布局位移：266 个函数带地址注解变化；`victim_context.cpp` 为新增编译单元。

## 归因评估

- 代码逻辑不触及 spray 阶段；两次 panic 与历史 `KERNEL-PANIC-01` 模式相同（CPP06c、CPP07、CPP12b/c/e 分别在 spray 阶段或 W2 后触发，位置不一致；无 dmesg/pstore 权限，根因不可判定）。
- 但本次是同一构建 **2/2 连续** panic，按 `PI-TIMEOUT-01` 先例不再扩大样本，先回退 VictimContext 收编并做隔离复跑，确认设备与攻击层状态。
- 设备温度与冷却间隔：**日志不可判定**（a/b 两次运行间隔约 1 分 42 秒，无温度记录）。

## 后续

- VictimContext 收编代码保留在提交历史（`c222151`），登记为待受控条件下重试的批次。
- KERNEL-PANIC-01 追加本次证据；回退后以 `cea1bedc` 等价构建隔离复跑。
