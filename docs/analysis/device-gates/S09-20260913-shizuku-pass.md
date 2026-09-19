# S09 真机门禁：Heap 与 PayloadPage 所有权通过

- 阶段结论：通过
- 验证路线：5.15 Multicast one-shot
- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 启动方式：Shizuku（`uid=2000`，`Seccomp=0`）
- CPU：main 0 / consumer 1
- 设备侧日志：`Download/GhostLock/ghostlock-20260913-121147-650-0-shizuku.log.txt`
- 原始日志：[S09-20260913-shizuku-pass.native.log](S09-20260913-shizuku-pass.native.log)

## 端到端结果

| 阶段 | 结果 | S09 所有权证据 |
|---|---|---|
| W1 SELinux | 通过 | 首次 heap prepare 成功，route `step=0/calls=1/success=1` |
| W1b scratch repair | 通过 | current 页面整体迁入 quarantine；新 current 完成修复后释放 quarantine |
| W2 credential | 第 2/15 次通过 | 两轮均完成 prebuilt 页面、current 页面和 W2b 激活流程 |
| W2b repair | 通过 | prebuilt 激活后两次 route 均为 `step=0/calls=1/success=1` |
| W3 | 按设计跳过 | Shizuku shell 无 app seccomp filter |
| KernelSU handoff | 通过 | `child uid = 0`、`KernelSU ready`，总耗时约 26.1 秒 |

六次 heap prepare 均在内部第 1 次尝试成功。日志未出现 payload 缺失、prebuilt 激活失败、quarantine 失败、重复所有权或 fd 相关错误，说明 S09 在已覆盖的 W1 quarantine 和 W2 prebuilt 两种关键状态迁移中保持了原分配、喷射与释放时机。

## 边界与结论

日志能够证明页面状态转换后攻击链行为兼容，但不会枚举进程内每个已关闭 fd；fd 的唯一关闭语义由 `heap_context_test` 固定验证。TCP Zerocopy 与 Select Stack 仍缺本地设备覆盖，沿用 S08 的外部协作者补证策略，不阻塞本阶段已验证的共享 Heap/PayloadPage 所有权改造。
