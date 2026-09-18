# S04 真机门禁日志分析

## 证据

- 原始日志：[S04-20260913-shizuku-pass.native.log](S04-20260913-shizuku-pass.native.log)
- 结果：通过
- 设备：Sony A301SO
- Android：15
- 内核 release：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口：Shizuku UserService；日志工作目录为 `/data/local/tmp/ghostlock-app`
- 运行身份：`uid=2000`、`u:r:shell:s0`、`NoNewPrivs=0`、`Seccomp=0`
- 路线：5.15 Multicast Waiter（由 5.x profile 与 `mcast ghost disarm` 路线日志判定）
- CPU：主线程 3，consumer 4；profile 推荐值为 0/1，本次使用 UI 显式选择值
- 温度条件：攻击结束后读取电池温度为 32.0°C、电量 100%；攻击执行期间温度日志不可判定

## 阶段结果

| 阶段 | 观察结果 |
|---|---|
| Profile/配置 | resolved profile 与运行内核 release 一致；33 项 execution 参数完整输出 |
| Heap/KernelSnitch | 所有记录的 heap prepare 均为第 1 次成功；碰撞筛选和 `mm_struct` 泄露均完成 |
| W1 | `SELinux attempt 1/1` 成功，进入 permissive |
| W1 scratch repair | 第 1/3 次成功并释放 quarantine |
| W2 | credential 第 1/15 次成功；prebuilt repair 随后完成，child UID 变为 0 |
| W3 | shell 流程没有 app seccomp filter，按设计跳过，而非失败或回退 |
| KernelSU 交接 | `KernelSU ready`，攻击总耗时日志约 13.735 秒 |

## 回退、清理与异常

- 未观察到 TCP→Select 或其他运行时路线回退；本次只执行 Multicast 路线。
- 每次 PI 路线都记录 `route_done ... success=1`，随后 `threads joined`，说明用户态路线线程完成回收。
- 每次 `mcast ghost disarm` 均返回 `ret=-1 errno=110`（`ETIMEDOUT`）；当前流程仍成功完成。这是既有兼容行为，不能据此证明 ghost 内核状态已完全解除。
- W1 scratch repair 明确记录 quarantine 已释放；W2 相关 quarantine/预构建对象的最终释放细节日志不可判定。
- 日志在 `KernelSU ready` 结束，未记录 native 进程最终退出及 `kernel5_resident_stop()` 结果，因此最终 resident stop 和进程级资源清理不可判定。
- 未出现 profile 解码错误、hash 断言、崩溃、黑屏或重启记录。

## S04 结论

S04 新增的显式 `FutexHashContext` API 未改变当前攻击链；KernelSnitch 仍通过旧兼容入口运行，实际迁移属于 S05。本次完整攻击链成功说明新增接口和兼容层没有引入可观察回归。显式 API 本身另由 A301SO 上四组固定 key/mm/table-size 向量验证，与旧截断函数及旧入口逐位一致。

