# 43284 的窗口门：open_doors / close_doors 该做什么（Lead 切片）

- 状态：**表**（依据已量事实）· 2026-10-09 · 上游 `seam-port-design-43284-slice1.md`
- 取名理由：`window-port` 的切片 1 五轮未落盘、回合已被 Lead 中断 ⇒ 为不撞它的文件名，本件另起一名，范围也缩到最小可做：**只定门**。

## 依据（全部已量，出处给到行）

- 旧树里门**不是 route 开的**，是**绑定层**开的：`execution_binding.cpp:204` 设 `ctx.page.file_fd = target_fd`；`:205` 设 `ctx.page.socket_fd = connect_esp_socket(sa)`；`:168` 设 `ctx.page.io = pagecache::real_splice_io()`；失败路径 `:210` 把 `file_fd` 置回 -1。
- 上下文其余字段：`pipe_flags` 在 `:211` 附近；`next_seq` 由 `pagecache.hpp:72` 起值 1，`:192` 写进 ESP 头，`pagecache.cpp:256` 写后推进。
- `pagecache.cpp` 对旧会话与两个全局的耦合 **0**（实测）⇒ 门这一段**不带任何全局**即可搬。

## `open_doors(sa)` 要做的事（最小集）

1. 连出 ESP 套接字：旧 `connect_esp_socket(sa)` ⇒ 只依赖 `sa`（参数里的凭据）⇒ 窗口内可做，不需要其它状态。
2. 取文件描述符：旧树来自 `target_fd`（绑定层给的）⇒ **谁先 open 的，我未量到** ⇒ 两种可能（窗口自己开 / 调用方给）⇒ **本件不裁**，列为下一条要量的事。
3. 装 splice 面：`io = real_splice_io()` ⇒ 无参数，窗口内可做。
4. 起序列号：旧默认 1 ⇒ 按 ADR-0015 这属于值由脚本给的一类 ⇒ **默认值不得写死在代码里** ⇒ 要么由参数带，要么具名拒绝。
5. 失败即退：旧树失败时把 `file_fd` 置回 -1（`:210`）⇒ 新的 `open_doors` 返回 false 并保证**不留半个门**（半个门比没门更危险）。

## `close_doors()` 要做的事

1. 关掉它**自己打开的**那两个描述符 ⇒ **只关自己开的**：若文件描述符由调用方给，这里关就是双重关闭。
2. 清 splice 面（`io` 置回空）。
3. 与 `PageCacheSeam::tear_down` 的顺序：**先关门、再清凭据**。

## 还没量（下一件，两三条足够）

- 旧树里 `target_fd` **是谁 open 的**（决定门的归属：谁开谁关）
- `connect_esp_socket` 的**失败语义**（返回 -1？errno？）
- `pipe_flags` 的旧值来源（`:211` 附近未逐行读）
