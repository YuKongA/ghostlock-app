# 43499 · `select_stack` 适配规格（round 167，供落码用）

## 一、退役 `SelectRoute` 的形状（快照 375-430 ✓）

| 成员 | 作用 | 本树对应物 |
| --- | --- | --- |
| `PiRace *race` | 三演员引擎 ✓ | `race::PiRace<Seam>` ✓（已有 ✓） |
| `const WriteRequest *request` | 写请求 ✓ | `memory::WriteRequest` ✓（已有 ✓） |
| `bool compact` | compact 词表开关（原 profile ✓） | 走**文档** ✓（新增键 ✓） |
| `waiter_shift`（默认 `-2` ✓） | fd_set word 0 对齐 waiter 起始 ✓ | 走**文档** ✓ |
| `fd_set in_set/out_set/ex_set` | **载荷载体** ✓（VS multicast 的 setsockopt ✓） | 新增 ✓ |
| `fd_set owned_*` | 自建（非 stdio 借位）的那部分 ✓ | 新增 ✓ |
| `pipe_read/pipe_write/block/high_read` | 低编号 fd + 永不就绪的 block fd ✓ | 新增 ✓ |
| `stdio_backup[3]` | dup2 前的备份（0/1/2 ✓） | 新增 ✓ |
| `block_borrows_pipe` / `selected_fds_installed` / `consumer_stuck` | 生命周期标志 ✓ | 新增 ✓ |
| `calls/successes/status` | 报告 ✓ | `MulticastRouteReport` 同形 ✓ |

## 二、waiter 词表（compact 支 ✓，快照 414-430 ✓）

```
put_waiter_word(2,  fake_right)      // tree_pc
put_waiter_word(3,  0)               // tree_right
put_waiter_word(4,  request->target) // tree_left
put_waiter_word(5,  fake_right)      // pi_pc
put_waiter_word(6,  0)               // pi_right
put_waiter_word(7,  request->target) // pi_left
put_waiter_word(8,  fake_task)       // task
put_waiter_word(9,  fake_lock)       // lock
put_waiter_word(10, (kFakeWaiterPrio << 32) | 3)  // wake_prio
put_waiter_word(11, 0)               // deadline
put_waiter_word(12, 0)               // ww_ctx
⇒ 与 multicast 的"元组"语义**同槽** ✓（tree/pi 双侧 + task/lock ✓）⇒ 值同样来自**假页** ✓
```

## 三、适配到本树（落码时的必做项 ✓）

1. 命名与风格照本树 ✓（`SelectStackRoute<Ops>` + `SelectStackRouteLinux` ✓，Doxygen 注释 ✓，禁用 using ✓）；
2. 数字**走文档** ✓：`compact` / `waiter_shift` / `kFakeWaiterPrio` / fd 数量（320 ✓）⇒ 新增 heap/route 键 ✓；
3. 复用既有件 ✓：`memory::write_layout` / `payload_page` / `race::PiWindow` ✓（**不重写** ✗）；
4. 报告与判据照 canonical ✓（`status.code/step` + `calls/successes` ✓）；
5. 首次落码**只加文件、不接线** ✗ ⇒ 判据：`android` 0 警 ✓、既有两条路不变 ✓；
6. 接线后再跑夹具：应从 `route-not-ported:select_stack` ✓ 变成本 route 的报告 ✓。

## 四、已知风险（落码时逐条核 ✓）

- **词表两套**（compact 6.1 / 6.6 ✗）：本机 5.15 ⇒ 必须按设备核 `rt_mutex_waiter` 布局 ✓（不臆测 ✗）；
- **fd 操作面大**（320 fd + dup2 + stdio 备份 ✓）⇒ 资源生命周期要按 AGENTS §5 逐条列（创建/所有者/借用/终结 ✓）；
- 奇偶校验**先于**路由选择 ✗ ⇒ 判据须多次统计 ✓。
