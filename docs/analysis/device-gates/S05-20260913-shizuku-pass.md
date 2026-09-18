# S05 真机门禁成功日志分析

## 证据与判定口径

- 原始日志：[S05-20260913-shizuku-pass.native.log](S05-20260913-shizuku-pass.native.log)
- 结果：通过；S05 构建身份由用户真机测试确认，Native 日志当前不输出 Git commit，无法仅从日志独立判定二进制版本
- 兼容门禁口径：攻击链属于竞态实现，不要求每个 CPU 组合确定性成功；S05 至少一次完整执行至 `KernelSU ready` 即确认具备成功路径
- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口/路线：Shizuku shell；5.15 Multicast Waiter
- CPU：主线程 3，consumer 4
- 温度：攻击期间日志不可判定；取证时电池温度 28.0°C

## 阶段结果

| 阶段 | 观察结果 |
|---|---|
| Profile/启动 | release 匹配，33 项 execution 参数完整；shell `Seccomp=0` |
| W1 heap/KernelSnitch | 碰撞、`mm_struct` 扫描、payload 均首轮完成；page prepare 约 952 ms |
| W1 | 第 1/1 次成功，SELinux 进入 permissive |
| W1 scratch repair | 第 1/3 次成功，quarantine 明确释放 |
| W2 prebuild | 第 1 次 page prepare 成功 |
| W2 | credential 第 1/15 次成功，prebuilt repair 完成，child UID 变为 0 |
| W3 | shell 无 app seccomp filter，按设计跳过 |
| KernelSU | `KernelSU ready`；攻击完成日志约 13.735 秒 |

## 回退、清理与异常

- 未触发路线回退；所有 PI 执行均记录 `route_done ... success=1` 和 `threads joined`。
- `mcast ghost disarm` 仍返回 `errno=110`，与 S04 成功基线一致；最终 resident stop 日志不可判定。
- 未出现 futex hash 初始化错误、KernelSnitch 状态错误、无效候选、崩溃、黑屏或重启记录。
- S05 曾在 0/1 核心组合下停于 `Write 1 failed`，见同目录失败报告；结合本次成功，按用户确认应视为核心/调度相关的竞态随机性，而非阻断 S05 完成的确定性不兼容。

## S05 结论

`KernelSnitchContext` 的 init/find/scan/result/destroy 生命周期及其拥有的 `FutexHashContext` 已在完整 5.15 Multicast 攻击链中至少成功执行一次。S05 真机兼容门禁通过；不同 CPU 组合的成功率差异保留为后续 profile 调优问题，不回滚本阶段对象化。

