# S06 真机门禁：地址状态与 Profile View

- 结论：通过
- 设备：Sony A301SO
- 内核：`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 启动方式：Shizuku（`uid=2000`，`Seccomp=0`）
- 路线：5.15 Multicast
- CPU：main 3 / consumer 4（profile 推荐 0 / 1）
- 测试后温度：约 35 °C
- 原始日志：[S06-20260913-shizuku-pass.native.log](S06-20260913-shizuku-pass.native.log)
- 设备侧归档：`Download/GhostLock/ghostlock-20260913-104843-562-0-shizuku.log.txt`

## S06 地址快照证据

| 项目 | 日志结果 | 判定 |
|---|---:|---|
| SoC 分支 | `qcom/other` | 命中预期分支 |
| `kernel_phys_load` | `0xa8000000` | 与 S04/S05 成功基线一致 |
| `init_cred` image | `0xffffffc082bfd588` | 与成功基线一致 |
| `init_cred` alias | `0xffffff802abfd588` | 与成功基线一致 |
| profile `phys_offset` | `0x80000000` | 正确 |
| profile delta | `0x28000000` | 与 load/offset 差值一致 |

以上结果表明 `TargetProfile` 只读视图、`ResolvedAddresses` 解析结果及旧全局量兼容镜像在该设备上保持等价。

## 端到端执行结果

| 阶段 | 结果 | 关键证据 |
|---|---|---|
| W1 | 通过 | collision、`mm_struct` 泄漏、payload/page 准备及 route 均成功 |
| W1b scratch repair | 通过 | 第 1/3 次尝试成功，quarantine 已释放 |
| W2 credential | 通过 | 第 1/15 次尝试成功 |
| W2b repair | 通过 | prebuilt repair route 成功 |
| 提权验证 | 通过 | `child uid = 0`、`child is root!` |
| W3 | 按设计跳过 | Shizuku shell flow 无 app seccomp filter |
| KernelSU handoff | 通过 | `KernelSU ready`，总耗时约 13.5 秒 |

所有已执行 route 均报告 `success=1` 且线程完成 join。日志中的 `mcast ghost disarm ret=-1 errno=110` 也存在于此前成功基线，不影响本次门禁结论。本次没有发生运行时跨路线回退；最终 resident stop/进程退出后的资源状态没有对应日志，因此该部分仅凭本日志不可判定。

## 门禁结论

S06 在 A301SO 的 5.15 Multicast 路线上保持地址解析、写入阶段、凭据阶段和 KernelSU 交接兼容，可标记完成。后续旧地址全局量和 `active_offsets` 兼容镜像仍按计划留到 S08 收敛。
