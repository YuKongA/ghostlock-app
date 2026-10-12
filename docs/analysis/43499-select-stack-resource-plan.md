# 43499 · `select_stack` 资源清单（AGENTS §5 ✓，接线前必备 ✓）

## 一、生命周期事实（快照逐行 ✓）

| 资源 | 创建 | 所有者/借用 | 终结 | 备注（快照行 ✓） |
| --- | --- | --- | --- | --- |
| `pipe_read` / `pipe_write` | `::pipe(fds)` ✓ | `SelectRoute` 自己 ✓ | `close` ✓ | 失败 ⇒ `fail(31, errno)` ✓（:496-498） |
| `block`（timerfd） | `SYS_timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)` ✓ | 自己 ✓ | `close`（**非借用**时 ✓） | 失败 ⇒ **借用 pipe 读端** ✓（`block_borrows_pipe` ✓；:499-502、:600） |
| `high_read` | `fcntl(block_fd(), F_DUPFD_CLOEXEC, NFDS+16)` ✓ | 自己 ✓ | 随所在 fd 一起 ✓ | 给 select 一个"永不就绪"的高位对象 ✓（:502） |
| 低编号 fd 的**借用** | `fcntl(fd, F_DUPFD, NFDS+64)` 备份 ✓ ⇒ `dup2(hr, fd)` ✓ | 借用中 ✓ | `dup2(stdio_backup[i], fd)` 还原 ✓ | **备份三件**：`stdio_backup[0..2]` ✓（:465-474） |
| `read_fd` 的安装 | `dup2(read_fd, NFDS-1)` ✓ + `FD_SET(NFDS-1, &ex_set)` ✓ | 安装中 ✓ | 由还原路径处理 ✓ | "被置位的低 fd 才 dup2" ✓（:447-458） |
| `hr`（临时高位副本） | `fcntl(read_fd, F_DUPFD, NFDS+32)` ✓ | 临时 ✓ | **立即** `close(hr)` ✓ | 不留悬挂 ✓（:447-456） |
| `in_set`/`out_set`/`ex_set` | 栈上 `fd_set` ✓ | 本帧 ✓ | 无（栈 ✓） | 载荷载体 ✓ |

## 二、关闭顺序（快照的守卫 ✓，必须照抄 ✓）

```
1. 每个被置位的 fd：**若同时被 pipe/timerfd 持有 ⇒ 先解绑再关** ✓（避免二次 close ✗，:586-591）；
2. 再关 pipe 两端 ✓（:597-598）；
3. 最后关 timerfd —— **仅当非借用 pipe** ✓（:600）；
4. 还原 stdio：`dup2(stdio_backup[i], i)` ✓（:474）。
```

## 三、本树对应物（复用 ✓，不重写 ✗）

| 快照 | 本树 |
| --- | --- |
| `PiRace`（三演员 + 窗口） | `race::PiRace<Seam>` + `race::WindowGuard` ✓（已有 ✓） |
| 写请求/布局 | `memory::WriteRequest` / `memory::write_layout` ✓（已有 ✓） |
| 假页 | `memory::payload_page` ✓（已有 ✓） |
| **fd_set 载荷 + 低 fd 借用 + timerfd** | **新增** ✓（本清单即为其生命周期契约 ✓） |

## 四、接线时必须逐条核的失败路径（§5 要求 ✓）

- 成功 / 失败 / 重试 / 取消 / **早退** 四条路径都要走到"还原 stdio + 关全部自有 fd" ✓；
- `dup2` 失败 ⇒ 立即还原已改的低 fd ✓（先备份后改 ✓）；
- 借用期间的任何 `fail(...)` ⇒ 走同一还原路径 ✓（不得只 `return -1` ✗）。
