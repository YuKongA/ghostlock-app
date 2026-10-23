# P1d 证据：util 页/喷溅/fd 搬入 session（2026-10-21）

> 从属 [P1 施工设计](../analysis/43499-p1-session-design.md)。目标：main/fops/util 的可写实现全局清零。

## 改动

已搬入 `s->rt`：

```text
ks / mm_objs_per_slab / skb_buf
reclaim_sv / quarantined_reclaim_sv / prebuilt_reclaim_sv
prebuilt_page_base + prebuilt_fake_*
prepare_ctx / spray_ctx / pre_ctx / post_ctx
child_leak / memfd_leak
page_base / last_mm_struct / fake_lock / fake_w0 / fake_task
fake_parent / fake_right / fake_left / fake_fops
```

- `pselect_custom_write/target/child_node` 删除；`prepare_skb_payload` / `prepare_pselect_fdsets` 只读 `GlkWriteRequest`。
- `set_pselect_write_mode` / `clear_pselect_write` 删除。
- util 辅助函数收 session：`close_reclaim_sockets(s)`、`cleanup_page_prepare_state(s)`、`prepare_ctxs(s)`、`stash/activate/discard(s)`、`clone_leak_child(s)`。
- 未使用的 kernelsnitch 包装函数删除。

## 验证

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
llvm-nm：page_base/fake_*/ks/skb_buf/reclaim_sv/prepare_ctx/memfd_leak/pselect_custom 无 B/D 符号
```

### cmp / important

```text
cmp_disasm strict：红（prepare_kernel_page shape 变化，参数传递导致）
cmp_disasm --count-delta 64：红（同 1 条 shape，非 count）
IMPORTANT-SUMMARY functions=55 base=220 cur=220
IMPORTANT-COUNT/ORDER FAIL: 0
IMPORTANT-TRACE DIFF: 69（session/结构地址或参数链）
```

69 条 arg diff 归类：

| 类别 | 原因 |
|---|---|
| `close@plt x0` | reclaim/prebuilt fd 从全局数组变 `rt->*` |
| `futex_op x0` | `mr_*` 移入 `s->rt` |
| `pthread_create/join` | 线程句柄/参数移入 session |
| `syscall/fcntl/waitpid` | `mm_ctx`/页状态移入 session |
| `setsockopt` | `mr_stamp` 的 `mr_fd` 来自 session |

结论：重要 call/syscall 数量/顺序不变；strict 等价未宣称。

## 剩余可写全局

```text
active_offsets / p0_kernel_phys_load / g_init_cred_image   -> P3
g_core_main / g_core_consumer                              -> P2
g_home_dir / g_root_script_path / g_external_*             -> P2/P3
g_file_buf / g_*_map                                       -> offsets_json P6
kernelsnitch_strings / futex_hashsize / known_offsets      -> 共享/只读，P4/P6
run_exploit.vr_needed                                      -> 函数静态，P1d 后续
```

日志：`build/analysis/cmp-disasm-p1d.log`、`build/analysis/important-p1d.log`。
