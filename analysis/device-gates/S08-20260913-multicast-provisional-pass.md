# S08 暂时门禁：Multicast 已验证

- 阶段结论：按用户决定暂时完成
- 已验证路线：5.15 Multicast
- 待外部协作者验证：TCP Zerocopy、Select Stack
- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 启动方式：Shizuku（`uid=2000`，`Seccomp=0`）
- CPU：main 0 / consumer 1（由 TargetProfile 推荐并实际采用）
- 测试后电池温度：约 31.0 °C
- 原始日志：[S08-20260913-multicast-provisional-pass.native.log](S08-20260913-multicast-provisional-pass.native.log)
- 设备侧归档：`Download/GhostLock/ghostlock-20260913-113606-111-0-shizuku.log.txt`

## Profile 与 execution 证据

日志完整输出并使用了 S08 接入的 `TargetProfile.execution`：heap 4 次/240 秒、KernelSnitch 60 秒、race 1000/50000/1000、W1 15、W2 15、W3 3×6，以及 Multicast 和 handoff 时序参数。实际选择核心 0/1 与 profile 推荐值一致。地址快照继续解析为 `kernel_phys_load=0xa8000000`、`init_cred image=0xffffffc082bfd588` 和 alias `0xffffff802abfd588`，与此前成功基线一致。

## Multicast 端到端结果

| 阶段 | 结果 | 重试/证据 |
|---|---|---|
| W1 | 通过 | 第 1/1 次成功，SELinux permissive |
| W1b scratch repair | 通过 | 第 1/3 次成功，quarantine 已释放 |
| W2 credential | 通过 | 第 4/15 次获得 uid 0 |
| W2b repair | 通过 | 每轮均执行 prebuilt repair |
| W3 | 按设计跳过 | Shizuku shell flow 无 app seccomp filter |
| KernelSU handoff | 通过 | `KernelSU ready`，总耗时约 40.4 秒 |

所有执行过的 route 均报告 `step=0`、`calls=1`、`success=1` 且线程完成 join。`mcast ghost disarm ret=-1 errno=110` 与既有成功基线一致。没有发生运行时跨路线回退。最终进程退出后的资源状态没有对应日志，因此为“日志不可判定”。

## 覆盖缺口与发布策略

当前没有可运行 TCP Zerocopy 或 Select Stack 路线的本地设备。这两条路线仅完成编译、TargetProfile 能力/布局固定测试和共享 payload 回归测试，不能视为真机通过。APK 将发布给外部协作者；收到结果后必须分别保存原始 Native 日志和分析，并回填设备、release、入口、CPU、温度、W1/W2/W3、回退、交接与清理信息。外部测试失败时应重新打开 S08，而不是直接进入相关路线重构结论。
