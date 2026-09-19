# S10 真机门禁：PiRaceContext 通过

- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口：Direct，main 0 / consumer 1
- 成功日志：[S10-20260913-direct-pass.native.log](S10-20260913-direct-pass.native.log)
- 前置随机失败日志：[1](S10-20260913-direct-w1-fail-1.native.log)、[2](S10-20260913-direct-w1-fail-2.native.log)、[3](S10-20260913-direct-w1-fail-3.native.log)

成功运行包含 6 个完整 PiRace 生命周期。每次都依次记录 creating、waiter parked/owner started、`route_done step=0 errno=0 calls=1 success=1` 和 threads joined，计数均为 6；最终 `child uid = 0`、`KernelSU ready`，总耗时约 28.5 秒。

成功前的三次运行均发生既有随机性导致的 W1 验证失败，但各自的 route 仍返回 `step=0/calls=1/success=1`，并在退出阶段完成 threads joined。它们没有出现线程创建错误、等待卡死或未 join，反向证明失败路径不会遗留本轮 PI worker。部分 `pthread_create` 系统性失败无法在真机正常环境主动制造，其显式 stop/join 分支由代码结构和构建确认。
