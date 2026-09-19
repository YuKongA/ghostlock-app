# CPP08 Shizuku 入口失败：route 卡住后内核 panic（日志通路背压）

- APK：`versionCode=210`
- 执行：21:30:07 与 21:31:53 各一次；分别在 21:31:05、21:32:43 触发 `kernel_panic`（第二次 `reason=bug`）
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Shizuku（shell uid UserService）；CPU main=0、consumer=1
- 温度：日志不可判定
- 用户报告：多数情况下卡在 `waiting route_done` 后死机重启
- 结论：失败（两次内核 panic）

## 根因分析

Shizuku 路径与 Direct 的**日志通路**不同，而这个差异落在攻竞态窗口内：

| | Direct | Shizuku |
|---|---|---|
| native stdout | `redirectOutput(文件)`，无背压 | **管道**，由 `GhostlockUserService` 逐行消费 |
| 展示路径 | App 本地 tail 文件 | **每条日志行一次同步 binder 回调**到 App |
| 敏感窗口风险 | 无 | 管道填满时 **native 的 write 阻塞**（waiter/consumer 线程同样写日志） |

`pi_race_run()` 的 `while (!route_done) usleep(...)` 没有超时；一旦 route 在竞态窗口内因日志背压卡住，
主线程永久等待，已经建立的 PI 链无人 disarm，随后触发内核崩溃。

两条日志在设备上仅剩 `debug log:` 头部、其余为空洞，也说明回传链在运行期严重滞后或中断。

Shizuku 本身的计算开销可忽略（native 二进制相同、核心固定逻辑相同）；破坏时序的是日志 I/O 路径。

## 修复

`GhostlockUserService` 改为与 Direct 同构：

- `ProcessBuilder(...).redirectOutput(nativeLog)`：native 写文件，**永不背压**；
- 独立 tailer 线程 `relayLog()` 按完整行异步转发（binder 只作为展示旁路，慢也不会阻塞攻击）。

## 待办

- 复测 Shizuku 入口（`versionCode=211`）。
- 可选加固：`pi_race_run()` 的等待加超时并把超时映射为 `ROUTE_DIRTY_FAILURE`（属攻击逻辑改动，需单独门禁）。

原始日志：

- [`CPP08-20260917-shizuku-panic-a.native.log`](CPP08-20260917-shizuku-panic-a.native.log)（21:31:53 执行）
- [`CPP08-20260917-shizuku-panic-b.native.log`](CPP08-20260917-shizuku-panic-b.native.log)（21:30:07 执行）
