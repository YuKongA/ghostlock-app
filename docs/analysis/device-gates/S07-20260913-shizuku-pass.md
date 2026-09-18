# S07 真机门禁：Payload/WriteRequest 构建器

- 结论：通过
- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 启动方式：Shizuku（`uid=2000`，`Seccomp=0`）
- 路线：5.15 Multicast
- CPU：main 0 / consumer 1（与 profile 推荐值一致）
- 测试后电池温度：约 31.9 °C
- 原始日志：[S07-20260913-shizuku-pass.native.log](S07-20260913-shizuku-pass.native.log)
- 设备侧归档：`Download/GhostLock/ghostlock-20260913-111823-037-0-shizuku.log.txt`

## S07 数据流验证

本次执行覆盖了不可变 `WriteRequest` 的两种关键布局：W1/W2 的 one-child 写入（`leaf=0`）以及 W1b/W2b 的 leaf 修复写入（`leaf=1`）。两类请求均经新的 payload layout/route encoder 路径成功完成实际写入。

| 阶段 | 请求 | 结果 | 关键证据 |
|---|---|---|---|
| W1 | zero、one-child | 通过 | 第 1/1 次成功，SELinux permissive |
| W1b | zero、leaf | 通过 | 第 1/3 次成功，scratch quarantine 已释放 |
| W2 prebuild | zero、leaf | 通过 | payload prebuilt 成功 |
| W2 credential | credential、one-child | 通过 | 第 1/15 次成功 |
| W2b | zero、leaf | 通过 | prebuilt repair route 成功 |
| 提权验证 | — | 通过 | `child uid = 0`、`child is root!` |
| W3 | — | 按设计跳过 | Shizuku shell flow 无 app seccomp filter |
| KernelSU handoff | — | 通过 | `KernelSU ready`，总耗时约 19.8 秒 |

四次实际 PI route 均报告 `step=0`、`calls=1`、`success=1`，并完成线程 join。日志中的 `mcast ghost disarm ret=-1 errno=110` 与既有成功基线一致。本次没有运行时跨路线回退。

## 失败背景与温度条件

本次成功前连续三次启动出现 W1 验证失败或异常重启；其中可读的异常重启发生在 W2 heap/collision 阶段、进入 `payload ready` 之前，其余日志被 NUL 覆盖而无法判定阶段。当时系统热管理记录机身温度约 44 °C，并对 CPU 限频。冷却后改用 profile 推荐的 0/1 核心，本样本一次完成全链。因此现有证据更符合攻击竞态受温度、调度和核心选择影响，不支持将此前失败归因于确定性的 S07 payload 字节差异。

## 清理与门禁结论

日志可确认每次 route 的三个线程均已 join，W1 scratch quarantine 已释放。最终 resident stop 和进程退出后的全部资源状态没有单独日志，因此该部分为“日志不可判定”。结合构建期 5 组新旧编码逐字节比较及本次完整真机链，S07 可标记完成；S08 前仍保留 profile/路线布局访问器 TODO。
