# 43499 攻击实现 vs legacy/very_legacy 只读差异审查（A301SO + multicast_waiter）

- 日期：2026-10-26
- 快照：HEAD `a9af5482`（2.0.11）+ 工作树未提交 Kotlin 重构（lifecycle/flow/debug-a301so.conf）；`core/*.c` 已在 HEAD，评审期间哈希一致；只读审查，未写仓库/未跑门禁。
- 审查人：native-core（只读岗）；记录：Lead。
- 总判：**新 Native 实现「结构已实现、物理未对齐」；与 legacy 物理语义非等价。当前从 UI/Flow 完全不可攻击（Kotlin 物理输入全空），即使补输入，A301SO multicast 仍有 3 个阻断级物理错误。**
- 本报告为精简记录；完整逐行矩阵（file:line）在 task-35 审查原文（会话记录）。

## 1. 等价/保持项（与 legacy2/very_legacy 一致）

- Placement：mm 塑形 syscall 序列/形状公式、先 pre/leak/post child→memfd→kill、KernelSnitch 流程（collisions→leak child 扫描→wait→bruteforce→`leak|=0xf<<56`→range 校验→base 对齐）
- Landing：commit 点 `sendmsg(pcp shaping socketpair)`；关闭阶梯 pre 全量 / post 尾 1 / spray slab 首，shaping pair，leak memfd，4 次 reclaim，关 prepare+杀 child；SO_SNDBUF=1MiB / reclaim=4 / SKB=0x10000
- Actor：futex LOCK_PI / WAIT_REQUEUE_PI / CMP_REQUEUE_PI / route_done / unlock chain 骨架；join 顺序 waiter→owner→consumer
- 三 Carrier：syscall 家族与触发方式（multicast `MCAST_BLOCK_SOURCE`；pselect fd_set；tcp punch+`TCP_ZEROCOPY_RECEIVE`）
- 所有权/关闭链：Actor stop→drain→disarm→join→Carrier→Page；JNI 三态回执与 carrier outcome 成功判据（新改进，非 legacy 等价物）

## 2. 非等价/阻断项（按严重度）

| 编号 | 严重度 | 位置 | 问题 |
|---|---|---|---|
| D1 | 阻断 | `kernel_internal.h:33,36-37,79`；`placement.c:423-427,649-659`；`TokenOwner.kt:298` | geometry 硬编码 `mm_struct_sz=0x500(1280)`/`objects_per_slab=25`/`snitch_collisions=4`，A301SO profile 为 `1024`/`32`/`8`；KernelSnitch bruteforce 步长错、heap 形状错，且 Native 强拒正确值 objects=32 |
| D2 | 阻断 | `TokenOwner.kt:325` vs `kernel_internal.h:63-68`；`placement.c:631-635` | Kotlin 默认 scan 上界 `0xffffffc000000000`(+256GiB) > Native `IDENTITY_END`(+64GiB) ⇒ 默认配置第一步必 INVALID_ARGUMENT；legacy 用 `direct_map_end=0xffffff9000000000` |
| D3 | 阻断 | `actor.c`/`carrier_multicast.c:103-154` | kernel5 multicast **ghost disarm 缺失**：legacy 路由后必做 `FUTEX_LOCK_PI(0x80000000|pid)` 移除 `pi_blocked_on` ghost（否则下次 spray 可能 panic）；新实现全树无此步骤 |
| D4 | 阻断 | `FinishHandoffAdapter.kt:101-108,160,176-178,199-208` | Kotlin 物理输入全 null、无 payload/地址/route 生成 ⇒ Flow 恒 inputs-incomplete；Native 实现无调用者 |
| D6 | 阻断 | `page.c:276-291,302-303,361-368` | SO_SNDBUF/O_NONBLOCK 设在 `reclaim_pair[0]`，sendmsg 却从 `reclaim_pair[1]` 发 ⇒ 1MiB 缓冲语义丢失、reclaim skb 形状改变（legacy 同 fd 设/发） |
| D5 | 重要 | `kernel_internal.h:89-92,127-139`；`carrier_multicast.c:122-135`；`actor.c:237-265,946` | mcast 缺 `arm_sequence=16`、`arm_hold` 4096≠20000、attempts 无来源；PI `settle 200us≠50000`、`poll 200≠1000us`、consumer burst/max 8/256≠1/1；compact nice 轮转 1..19 未实现 |
| D7 | 重要 | `page.c:299-300,313-318`；page 契约 | 页接受/arm-match 判据（selinux_state.initialized 偶数位）消失，坏页直接进写路径（panic 风险）；该职责按契约归 Kotlin，但 Kotlin 未实现 |
| D8 | 重要 | `actor.c:955-1016,1136-1143`；`Flow43499Impl.kt:229-244` | PI 窗口超时后 retained dirty 但进程继续；legacy 对活 PI worker `exit_group(70)` 不再继续（后续 spray 风险） |
| D9 | 重要 | `carrier_pselect.c:466-481,282-299` | consumer stuck 时 restore_backups（dup2/close 被覆盖 fd），legacy 则保留全部 fd 不 close/不恢复 |
| D10 | 重要 | `carrier_multicast.c:54-73,103-154`；`carrier_pselect.c:436-456` | mcast 需 Kotlin 逐字节写 family@8/task@144/lock@152/264B；pselect 需三 set+waiter words，legacy compact per-attempt 重喷/重建/延迟阶梯无对应 |
| D11 | 重要 | `kernel_internal.h:112-117`；`carrier_tcp.c:236-246,377-390` | tcp zc 布局 0 vs 0x18 矛盾；mapping_offset 未强制=page_size；arm_sequence/post_hold/sendbuf 缺失 |
| D12 | 重要 | `debug-a301so.conf:30-31,33-39,92-95`；`Flow43499RunSpec.kt:27-34` | A301SO debug 配置对新契约不完整：缺 execution.race/heap、multicast attempts/arm_sequence/arm_hold（对应资产已不在树）、phys_load/offset=null 无回退；runSpec 无 route/kind 字段 |
| D13-D15 | 建议 | 注释/线程创建顺序/nice 语义 | 过期注释、consumer→owner→waiter 顺序 vs very_legacy 不同、Actor 三优先级无 legacy 对应 |

## 3. 必须由 Kotlin 提供的物理输入（→ legacy 计算位置）

- PlacementRequest：profile 的 mm_struct_sz/order/partials + execution heap/race 参数（very_legacy `glk_session.c:68-124`、`util.c:563-576,598-605`、`support.cpp:555-725`）
- LandingRequest + finalBytes(0x10000)：very_legacy `util.c:389-569`、legacy2 `support.cpp:417-549`、`target.h:34-42,258-277`
- PrimitiveSpec 三地址 + frameBytes：mcast `base+0x400 / base`、264B stamp（task@144/lock@152/family@8）（`support.cpp:425-438`、`fops.c:136-147`）；pselect/tcp 见 §2 D10/D11
- 目标地址派生：`data_alias(image)=((phys_load+image-KIMAGE_TEXT_BASE)-P0_PHYS_OFFSET)|P0_PAGE_OFFSET`（very_legacy `util.c:132-140`、`legacy/memory/address_space.cpp:87-137`）；A301SO 需 `P0_KERNEL_PHYS_LOAD`/`P0_PHYS_OFFSET` 编译期回退
- ActorSetup：三 CPU/三优先级/超时（profile execution；legacy `5.15.conf:4-8`）；compact nice 轮转
- 各 spec 预算：attempts/buffer/arm/hold/punch/sendbuf/select nfds/timeout（`glk_session.c:68-124`、`execution-*.conf`、`legacy2 params.h`）

## 4. 接 debug 配置注入前的最小修复集（P0，需 L 批准）

1. D1：geometry 改为 profile 驱动（mm_struct_sz=1024→objs=32、collisions=8），去掉硬编码强校验；
2. D2：scan upper = `direct_map_end 0xffffff9000000000`（Kotlin 默认与 Native 校验一致）；
3. D3：补 kernel5 multicast ghost disarm（路由后 dummy PI，失败回收语义同 legacy）；
4. D6：reclaim 同 fd 设 SO_SNDBUF/O_NONBLOCK 并发送；
5. D4/D7（Kotlin multicast 最小生成器）：stamp+三地址+landing bytes+页接受判据；Native 侧恢复页接受拒绝/重取或 Kotlin 生成前拒绝；
6. D12：debug-a301so.conf 补 execution.race/heap、multicast attempts/arm_sequence/arm_hold、phys_load/offset 回退；runSpec 增加 route/kind 显式字段；
7. D5：arm_sequence=16、arm_hold=20000、settle=50000us、poll=1000us、consumer burst/max=1、compact nice 轮转（与 D1/D3 同批，避免“修了常量但时序仍错”）。
8. pselect/tcp 在 debug 注入批**禁用**（D9/D10/D11 后置）。

## 5. 修正：Native 全参数化（用户裁定 2026-10-26）

用户指出：**已明确禁止 Native 硬编码数值，全部参数化**。D1/D2/D5/D6/D11 只是症状；根因是 `core/kernel_internal.h` 仍有约 40 个 `GLK43499_PHYS_*` 硬编码宏（page_size/allocation/slab_order/mm_struct_size/objects_per_slab/skb_send_size/skb_reclaim_sends/mm_partials/prepare_slabs/identity_start,end/leak_tag/snitch_collisions/max_measurement_rounds/multicast max_frame,arm_hold/select nfds/tcp mapping,frame,punch_spin,post_hold,zc_address_offset/fill_byte/consumer max,burst,lock_pi_spin/poll_us/setup_settle_us/requeue_expected/disarm_spin_limit）。

因此 **P0 修正为：先做 Native 去硬编码、全参数化**，再谈 debug 注入：

1. `kernel_internal.h` 删除全部目标相关物理宏；只保留编译期结构性上限（数组容量/帧缓冲容量等），注明“非目标参数”。
2. 所有物理参数进入 ABI 请求结构（placement/page/actor/carrier 各自 struct，必要时升 schema 版本），JNI 从 Kotlin DTO 编组；Native 对入参做范围校验并 fail-closed，不得回退默认值。
3. 参数清单（按结构归属）：
   - Placement：page_size、allocation_size、slab_order、mm_struct_size、objects_per_slab、skb_send_size、skb_reclaim_sends、mm_partials、prepare_slabs、max_objects_per_slab、identity_start/end、leak_tag_nibble/shift、snitch_collisions、max_measurement_rounds、fill_byte
   - Multicast：max_frame、arm_hold、arm_sequence、attempts（后两者现缺失，同批补）
   - Pselect：min/max_nfds、timeout、per-attempt delay ladder
   - TCP：max_mapping、max_frame、punch_spin、post_hold、zc_address_offset、receive_size、fill_byte
   - Actor：consumer_max、consumer_burst、lock_pi_spin_ns、poll_us、setup_settle_us、requeue_expected、disarm_spin_limit、三 CPU/三优先级/超时
4. Kotlin 侧：`Cve43499RequestFactory`/DTO 显式携带上述参数；A301SO 数值来源为 `debug-a301so.conf`（debug 阶段由 Kotlin 读取，不接 profile registry）；缺参数 ⇒ fail-closed，不构造请求。
5. 验证：`glk43499` host 构建零告警 + 参数化单测（非法范围拒绝/正确值透传）+ Kotlin DTO↔JNI 编组测试；之后再谈真机门禁。

## 6. 审查状态

- 已验证（静态）：上列 file:line 逐行核对；profile 值（debug-a301so.conf 1024/8）；Kotlin/Native 常量对拍（D1/D2）；Kotlin 输入确为 null；ghost disarm grep=0；reclaim 发送端；旧 tcp 物理向量 0x18/0x20/0x28/0x30 双处一致；公共 C11 头未改。
- 未验证：真机冷机门禁/命中与写验证；编译（工作树 Kotlin 在 HEAD 后有未提交改动）；KernelSnitch 在 1024/1280 差异下失败形态；ghost disarm 缺失是否必然 panic；TCP zc 偏移真值；dirty retained 后内核稳定性；pselect fd 恢复安全性；失败注入/取消/回滚矩阵；任何 uid=0 成功证据。
