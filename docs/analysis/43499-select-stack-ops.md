# 43499 · `select_stack`：syscall 面（ops）已落地并进构建（round 176）✓

## 落地（设备侧 ✓，加法式 ✓）

| 方法 | 语义 | 备注（与快照一致 ✓） |
| --- | --- | --- |
| `pselect_now(nfds, in, out, ex)` | 三组 fd_set 的 `pselect` ✓ | **超时用 `nullptr` = 无限等** ✓（零 timespec 是"立刻返回" ✗ —— 两者不可混 ✓） |
| `make_pipe(read, write)` | `::pipe` ✓ | 失败回 errno ✓（对应快照 `fail(31, errno)` ✓） |
| `make_idle_timer()` | `timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)` ✓ | 永不就绪 ⇒ 让 select 一直等 ✓ |
| `duplicate_above(fd, floor)` | `F_DUPFD` ✓ | 备份原值用 ✓（快照 `NFDS+64` ✓） |
| `install_at(src, dst)` | `dup2` ✓ | 低编号借用 ✓ |
| `close_fd(fd)` | 幂等关 ✓ | 负数直接回 ✓ |
| `yield()` | `yield` 内联汇编 ✓ | 与退役自旋一致 ✓（**不产生系统调用** ✓） |
| `last_errno()` | 紧随调用取 errno ✓ | 判据要用 ✓ |

## 进构建的方式（登记 ✓）

```
头文件不会被单独编译 ⇒ 由 main.cpp **包含**（只为进 android 构建 ✓，尚未接线 ✗）；
效果：NDK 门禁（-Wall -Wextra -Wconversion -Wsign-conversion）**真正编译**了它 ✓ ⇒ RC=0、0 警 ✓。
```

## 判据（本轮 ✓）

```
make -C core -B android ⇒ RC=0，**0 error / 0 warning** ✓；check → RC=0，108 PASS ✓；lint → 0 ✓
既有两条路行为不变 ✓（只是多包含两个头文件 ✗ 无调用者 ✓）
```

## 移植进度

| 步骤 | 状态 |
| --- | --- |
| 1-2. route 核心 + 词号数据化 + 偏移换算 | ✓ |
| 3a. fd_set 组装（纯 ✓） | ✓ |
| 3b-1. fd 借用账本（纯 ✓） | ✓ |
| 3b-2. **syscall 面（ops ✓）** | ✓ 本轮（编译通过 ✓） |
| 3b-3. 把两者接成 `SelectStackRoute<Ops>`（三演员 + 真 close/dup2 路径 ✓，契约见资源清单 ✓） | ✗ 下一步 |
| 4. 接线选择器 | ✗ |
| 5. 三级真机判据（多次统计 ✓） | ✗ |
