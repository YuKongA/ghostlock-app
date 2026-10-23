# 早期 C 与 C++ 稳定性差异研究（2026-10-21）

## 结论先行

C++ 版约 90% 的稳定性，不是 C++ 语言本身，而是它承载了一系列早 C 分支没有同步的
**生命周期/边界/重试修复**。本次已定位到一个直接回归：

```text
早期 C 文件级 static：
  reclaim_sv[2]           = {-1,-1}
  quarantined_reclaim_sv[2] = {-1,-1}
  prebuilt_reclaim_sv[2]  = {-1,-1}
```

（早期 Xperia v17 补丁，提交 `c92c98ef`，`src/core/util.c`）

当前 session 化后这些字段在 `Glk43499Runtime` 里被 `memset(0)`：

```text
reclaim_sv[0]=0
quarantined_reclaim_sv[0]=0
prebuilt_reclaim_sv[0]=0
```

而所有判定都是 `>= 0` 表示“已占用”：

- `quarantine_reclaim_sockets()` 看到 `quarantined_reclaim_sv[0] == 0`，直接返回 0；
- `stash_prebuilt_page()` 看到 `prebuilt_reclaim_sv[0] == 0`，直接返回 0；
- `retry_write_stage()` 于是打印 `W2 fast repair prebuild failed` 并 **return 0**；
- 外层只跑 1 轮就打印 “W2 failed after 15 rounds”。

这正是用户看到的 W1 scratch quarantine 失败与 W2 只跑 1 轮。

## 已实施修复

1. `glk_session_create_43499` 新增 `runtime_init`：
   `reclaim_sv/quarantined_reclaim_sv/prebuilt_reclaim_sv = -1`，
   `memfd_leak/mr_fd = -1`。
2. `retry_write_stage` 不再因 prebuild/stash/activation/repair 单次失败直接 return；
   失败后继续本轮 `do_one_write` 与 verify，让 15 轮真正运行。

## 历史修复映射

| 历史提交 | 修复 | 当前 C 状态 |
|---|---|---|
| `c92c98ef` | 早期 Xperia v17 prebuilt 三组 fd 初始化 -1 | **本次恢复** |
| `aaaa6503` | possible CPU 决定 futex 表（2048） | **本次恢复** |
| `f8a50857` | 状态标签补全 | **本次恢复** |
| `5dd1a3f9` | slab shift 保持 size_t | 已有 |
| `994ccf81` | 回收 snitch child 时 close leak memfd | 已有 |
| `396e52d` | direct map 末端 / 扫描上限 | 部分：当前用 IDENTITY_END 封顶 |
| `993f399e` | compact select 失败后新页重试 | 待核对 |
| `43238c52` | W3 leaf probe 失败后退休 child | 待核对 |
| `452bd1fd` | 泄漏搜索地址顺序 + 防 spin | 待核对 |
| `b8ce395` | 攻击资源生命周期强制 | 部分（session 化已覆盖） |
| `c6776ad` | 验证是有界重试，patience 由调用方给 | 待核对 |
| `277579ee` | 43284 retry send，不只重读 | 43284 路径待核对 |

## 运行验证

修复 fd sentinel 后 warm run：

```text
W1b: private scratch repair attempt 1/3
private scratch repaired; releasing quarantine
W1: SELinux ... (already permissive)
W2: cred attempt 1/15
W2 fast repair payload prebuilt
child is root!
exploit complete
```

说明：

- W1 scratch quarantine 已通过；
- W2 prebuild 已成功；
- 用户报告的“只跑 1 轮”已消失；
- 但该次进程最终 `rc=255`，设备随后 `kernel_panic,null` 重启；
  第二次 warm run 也在 W2 route 处 panic。

后续在 2026-10-22 冷机复跑 **PASS**：`child is root!` + `KernelSU ready`，
设备未 panic。记录见 [20261022-cold-43499-pass.md](device-gates/20261022-cold-43499-pass.md)。
单次 PASS，长期成功率仍待多轮统计。

## 研究判断

C++ 的 90% 来自：

1. **fd/资源 sentinel 与所有权**（RAII 自然给 -1 初始值）；
2. **KernelSnitch 表尺寸/扫描边界**（possible CPU、direct-map end）；
3. **有界重试/失败后继续**，而不是一次失败就 return；
4. **回收顺序**（memfd close、W3 child retirement、fresh page retry）。

不是来自 STL/类/命名空间本身。当前 C 分支应继续保留 C 结构，但逐条回移这些行为修复。

## design-critic 复核（2026-10-21）

结论：fd sentinel 方向成立，但仍有 4 个阻断：

- F1：`mm_ctx.memfds`/`memfd_leak` 仍用 0 作 sentinel；已改为 alloc 后全 -1、close >=0。
- F2：prebuild/activation/repair 失败所有权未闭合；已让 discard 清空 prebuilt 字段，
  activation 失败 discard，repair 失败停止本轮 W2。
- F3：critic 快照早于本次 KernelSnitch 回移；当前源码已确认
  `futex_hash_table_size_for(possible)` 与 `collisions not found` 存在。
- F4：W2 route、W2b repair、post-root、KSU late-load 未分层；两次 panic 不能合并归因。
  仍待分层 debug gate 与 pstore/bootreason 证据。

critic 归因：C++ 90% 来自 fd/资源 sentinel、表尺寸/扫描边界、失败后继续/有界重试、
回收顺序；不是 C++ 语言本身。P0：`c92c98ef`、`aaaa6503`、`f8a50857`、`994ccf81`、
`5dd1a3f9`；P1：`396e52d`、`993f399e`、`43238c52`、`452bd1fd`、`c6776ad`、
`b8ce395`、`277579ee`。

## 建议批次

- P0：fd sentinel + W2 retry（本轮已完成）。
- P0：cold gate 回归，确认 1 轮问题不再出现。
- P1：`993f399e`、`43238c52`、`452bd1fd`、`c6776ad` 逐条对比回移。
- P1：post-root/W2 panic 单独抓 `ro.boot.bootreason`、pstore/dmesg（需 root/冷机）。
- P2：只把 C++ 的类型化/RAII 作为设计参考，不做整树合并。
