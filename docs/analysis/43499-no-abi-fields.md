# 更正二：43499 的访问参数里没有 ABI 数字（2026-10-15）

## 真实字段（`access_params.hpp:50-110` 逐行读出来的）

```text
route（RouteId，Unset 被拒）
offset, offset_set
stamp_waiter_offset, stamp_task_offset, stamp_lock_offset, stamp_buffer_size      （u64）
max_attempts, arm_hold, arm_sequence                                             （u32）
park_ms, settle_us, consumer_delay_us, burst_calls, max_calls, fallback_lock_us  （u32）
nice_modulus, policy                                                             （u32）
fake_task, fake_lock, target, value                                              （u64）
判定：assembly_complete()（看 fake_task/fake_lock 等是否齐）、usable_for_initialize() = route != Unset
```

⇒ **没有任何 ABI/布局字段** ✗ —— 那 10 个数字（payload 偏移 / 内核物理数 / 镜像偏移）属于
`layout_from_document(document, arithmetic)` 那条**单独的**链路（输入是文档 ✗，输出是 `LayoutFromDocument` ✓）。

## 因此更正我在 161/162 两轮里的两句话

| 我说过 | 实际 |
|---|---|
| 「43499 不需要 document」 | **一半对**：`usable_for_initialize()` 确实只要 route ✓；但**布局数字**仍来自文档那条链路（若走到那一步才需要） |
| 「把 ABI 数字放进 Access43499Parameters」 | ✗ 该结构没有这些字段，硬塞要改结构；更稳的是等布局那一步真正接进来时再定它们去哪 |

## 对下一步的影响（让工作变小了）

**`initialize` / `write` / `read` / `shutdown` 只需要 `route` + `offset`（+ 各 route 自己的键与时延）** √
⇒ 我上一轮加的 `Access43499Abi`（10 个字段）**暂时用不上**，但它不是错：
等 `layout_from_document` 那条路真接进来时，那 10 个数字正好就落在这个组里（届时要么喂参数、要么删组 ✓ 二选一，别让它悬着）。

**下一步照旧**：写 `device_initialize_43499(accessId, route, offset, offset_set)`（不带 abi ✗）、
`device_write_43499(accessId, address, block, why)`、`device_read_43499(…, why)`，通道 `AccessChannel::PiWindow` ✓。
