# S8b-2/3 证据：payload 与 fake 结构参数化（2026-10-21）

> 设计：[s8b-layout-replace-design](s8b-layout-replace-design.md)。目标：payload/fake 结构只读 layout。

## 改动

`util.c` 的 `prepare_skb_payload` 全链替换：

```text
LOCK_OFF / W0_OFF / FOPS_TABLE_OFF
RIGHT_OFF / LEFT_OFF / FAKE_TASK_OFF / CRED_COPY_OFF
TCP_FAKE_TASK_OFF / TCP_CRED_COPY_OFF
SKB_DATA_DELTA / SKB_FRAG_BIAS
FAKE_WAITER_* / FAKE_TASK_* / FAKE_TASK_PRIO / FAKE_WAITER_PRIO
FAKE_TASK_UCLAMP_*
```

全部改为 `L->...`：

```c
payload_geometry
payload_init_fake
payload_write_waiter
payload_write_task
```

`fops.c` 的 pselect `wake_prio` 改用 `L->fake_waiter_prio`。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

flatten S1：base=19 cur=19 args_diff=16 PASS
flatten S2：base=11 cur=11 args_diff=10 PASS
flatten S4：base=17 cur=17 args_diff=13 PASS

llvm-nm 43499 b/B = 0
```

## 剩余固定值

传输/喷溅几何仍为宏：

```text
TCP_PUNCH_SHMEM_LEN / TCP_ROUTE_ATTEMPTS / TCP_ARM_SEQ
TCP_POST_GETSOCKOPT_HOLD / PSELECT_ROUTE_NFDS
PSELECT_TIMEOUT_SEC / PSELECT_TIMEOUT_USEC
SKB_SEND_SIZE / SKB_RECLAIM_SENDS
ORDER3_SIZE / MM_ORDER / MM_PARTIALS
```

这些属于协议/喷溅几何，S8c 再决定是否进 layout 或 config。

## 文档

- [S8b-1](s8b1-image-param-split.md)
- [S8b 设计](s8b-layout-replace-design.md)
- 日志：`build/analysis/flatten-s8a-s1.log`
