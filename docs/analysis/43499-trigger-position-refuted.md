# 43499 · 触发位置的两种取法都试过（round 94）

## 事实

| 触发位置 | 等待者 park 返回 | 写入（bisect） | 路由状态 |
| --- | --- | --- | --- |
| park **之后**（原位置，本轮） | `ret=-1 errno=1`（EPERM） | `at2000=0x0` ✗ | `0/0` ✓ **健康** |
| park **之中**（上轮） | `ret=-1 errno=1`（EPERM） | `at2000=0x0` ✗ | `2/61` ✗ |

⇒ **位置不是判别项** ✗（两者都无写入）；且回到原位置后那个 `2/61` 异常**消失** ✓、状态回到 `0/0` ✓。

## 已排除项清单（本晚累计，全部真机验证 ✓）

1. 页面内容、元组 `{pc=cred,right=0,left=target}`、`prio=140`、`list_link=fake_task|1` ✓ 逐字节证对；
2. 页面在内核 ✗→✓ 已修（`page sent value=12672` ✓）；
3. 凭据别名空间（搬到 `0xffffffc0…` ✓）、三个 task 锚（`init_task`/`root_task_group` ✓）；
4. 引擎契约、`nr_requeue`/`val3`、`owner_started`、两相位、节拍、park 时长（60 ms 试过 ✗）；
5. 三步全执行、脚本顺序、队列 ✓；二分实验：写目标就在自家缓冲内 ⇒ 内核**一个字节都没改** ✗；
6. 触发位置（中 vs 后）✗。

## 剩下的候选（都很小，未比对的仅两处）

1. **stamp 的字节** ✗ 未 dump 过：`family` 字、`waiter_offset/task_offset/lock_offset/buffer_size` 与几何的对应 ✓；
2. **触发用的 socket 与选项** ✗ 未与退役逐字对：退役 `socketpair(AF_UNIX, SOCK_STREAM)` + `sendmsg`/
   `setsockopt(IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, sizeof(stamp))` ✓ —— 本引擎 `open_datagram()` ✓ 需核。
