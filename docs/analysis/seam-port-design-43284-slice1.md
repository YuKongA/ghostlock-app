# 切片 1（43284）：上下文到缝的映射表

- 状态：**表**（动代码前先出）· 2026-10-09 · 上游 `seam-port-design-43284.md`
- 取材：`git show b0e9f499^:src/core/backend/cve_2026_43284/pagecache/pagecache.hpp` 等（只读）

## 映射：`PageCacheWriteContext` 的每个字段归谁

- `sa`（`IpsecSaParams`：spi、aes_key[32]、hmac_key[32] 等）⇒ **运行时凭据**。其文件头原文写明密钥材料 arrive through the runtime channel，**不是 profile** ⇒ 与 ADR-0015 不冲突；旧树由**终端**填（`backend_terminal.cpp:183/447`、`backend_terminal.hpp:144/157`，并有 zeroize 语）⇒ 新形状里由**终端或脚本**提供，**访问只接收**
- `file_fd` / `socket_fd` ⇒ **`bring_up` 准备**（一条 route 的两个门）
- `next_seq` ⇒ **`bring_up` 起值**，`land_word` 每次写后推进（推进语义**未逐行读**）
- `pipe_flags` / `io`（`SpliceIoOps`）⇒ **`bring_up` 准备**（splice 面）
- `old_page` / `helper_write`（可选的替代来源）⇒ **可选**，旧树由终端在直接读失败后绑定；新形状默认不绑（保持直接路径）
- `stats`（`PageCacheWriteStats`）⇒ **访问自己维护**（写入/失败计数）

## 宽度：这一条要与 43499 分开

- 43499：`supports_width(8)`，窗口写一个字
- 43284：`supports_width(16)`，页缓存写一块（`write16` / `write_block`，`len` 必须是 16 的倍数，`len == 0` 是空操作 Ok）
- 两者用同一张接口，靠 `supports_width` 回答，不靠约定

## 缝的四格落位

- `bring_up` = 备 `file_fd` / `socket_fd` / `pipe_flags` / `io` / `next_seq`，并**接收** `sa`（或按脚本给的凭据装填）
- `tear_down` = 拆除上述并 `zeroize`（旧树已有该函数，写明 session secrets through zeroize）
- `land_word` = `write16(ctx, file_offset, bytes16)`，地址即 `file_offset`，值即 16 字节块
- `can_read` / `read_word` = 旧树另有 `real_read_block`（`real_ops.hpp:53`）⇒ **可以声明可读**，与 43499 的读同规则（先声明、备好事实、否则按名拒绝）

## 还没量（动代码前必须补）

- `write16` 内部对 `next_seq` 的**推进语义**（本次只看到 `InvalidArgument` 系列的判据，未读到推进处）
- `file_fd` / `socket_fd` 在旧树是**谁打开的**（终端还是 route 自己）——决定它们属 `bring_up` 还是属**脚本传入**
- `write16` 对 `old_page` / `helper_write` 为空的**行为**（未绑定时走直接路径的判据在哪一行）
