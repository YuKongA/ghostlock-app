# S05 真机失败日志分析

## 证据

- 原始日志：[S05-20260913-shizuku-fail.native.log](S05-20260913-shizuku-fail.native.log)
- 结果：失败，终点为 `Write 1 failed`
- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口/路线：Shizuku shell；5.15 Multicast Waiter
- CPU：主线程 0，consumer 1
- 温度：日志未记录攻击执行期间温度；读取日志后电池温度为 30.0°C

## 执行阶段

| 阶段 | 结果 |
|---|---|
| Profile | release 匹配，execution 快照完整 |
| KernelSnitch 碰撞 | 成功；early screen `16/7 at 31%`，约 1.874 秒完成 |
| `mm_struct` 扫描 | 成功，候选为 `0xffffff8a19d4f400` |
| Payload/heap | 第一次 prepare 成功，约 2.136 秒 |
| PI/Multicast route | `route_done ... calls=1 success=1`，线程均已 join |
| W1 验证 | SELinux 未变为 permissive，记录 `Write 1 failed` |
| W1b/W2/W3/KernelSU | 未进入 |
| 最终清理 | 路线线程 join 可确认；W1 失败后的完整进程级清理日志不可判定 |

## 与 S04 成功日志对比

| 项目 | S04 成功 | S05 失败 |
|---|---:|---:|
| 主/consumer CPU | 3 / 4 | 0 / 1 |
| 首次碰撞完成 | 834 ms | 1874 ms |
| 首次 `mm_struct` 扫描 | 979 ms | 1989 ms |
| 首次 page prepare | 1020 ms | 2136 ms |
| PI route 状态 | success=1 | success=1 |
| W1 写入验证 | complete | failed |

S05 的显式 hash context 使用 `256 × 在线 CPU`，与旧 `futex_init()` 的 mask 相同，并已用固定向量逐位验证。当前日志也显示碰撞发现和候选扫描均完成，没有 hash 初始化失败、无效 bucket 或扫描失败迹象。因此现有证据不支持 hash 结果回归。

最显著的可控差异是 CPU 从 3/4 改为 0/1，且 heap/碰撞阶段耗时约翻倍。失败发生在路线声称执行成功之后的目标写入验证，更符合 Multicast 竞态窗口未命中。下一次复测应先恢复 S04 的 3/4 核心并保持冷机，其余条件不变；若仍稳定失败，再添加 context/legacy 双算断言和候选 bucket 日志定位 S05 差异。

