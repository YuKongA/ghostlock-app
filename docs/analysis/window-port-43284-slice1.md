# 切片 1（43284）：`pagecache.cpp` 305 行的逐行去向表

- 状态：**表**（动代码前先出）· 2026-10-09 · 上游 `seam-port-design-43284.md` §7–§10、`seam-port-design-43284-slice1.md`；与 Lead 的 `window-port-43284-doors.md` 并行、不冲突
- 取材：`git show b0e9f499^:src/core/backend/cve_2026_43284/pagecache/pagecache.cpp`（`nl -ba` = 305 行；只读，未写回工作树）。下表 **:NN 均指该旧件行号**
- 去向五类：**W** 搬进窗口体 · **O** 搬进 `open_doors` · **C** 搬进 `close_doors` · **N** 不搬 · **S** 依赖脚本输入（ADR-0015）
- 计行：区间 1–305 **无缝隙**；空行/闭合括号随其所属构造（脚注 A）；`[S]` 是**值来源标注**，不是独立去向

## 1 逐行去向

| 旧行 | 内容 | 去向与依据 |
|---|---|---|
| 1–7 | 文件头注释；`#include` 旧模块头 | **W**；:7 改指新缝头（`seam/pagecache_seam.hpp`）；旧 `pagecache.hpp` 不搬（其上下文已被 `SaMaterial`＋窗口状态取代，见 `-slice1.md`） |
| 8–14 | 标准头 `<array> <cerrno> <cstddef> <cstdint> <limits>` | **W**（`<cerrno>` 服务 -EINTR 重试；`<limits>` 服务写前区间判据） |
| 15–19 | 匿名 ns；`kHeaderIvBytes` / `kIcvScratchBytes` | **W** |
| 20–31 | `page_cache_ready`：io 面 / 门 / `icv_len` / 来源 四判据 | **O**（谁开门谁判可用；:27–29 = 凭据校验 `[S]`；:30 后半见 N） |
| 32–38 | `store_be32` | **W** |
| 39–56 | `vmsplice_all` | **W** |
| 57–78 | `splice_span`（16 字节目标页 splice） | **W** |
| 79–90 | `splice_one_datagram` | **W** |
| 91–101 | `close_pair`（**每次写**的管道对） | **W**（与会话门 C 不是一件事） |
| 102–114 | `file_cache_write16_adapter` + ns 收 | **N¹**（旧 `contract::FileCacheWriteOps` 适配层；新树已无该类型；等价物＝窗口虚函数 `seam:48`＋`Access::write`） |
| 115–152 | `read_block` 的直接 `read_at(file_fd)` 段 | **W**（**写路径内部依赖**：`write16:182` 读旧密文块；不是公开读面） |
| 153–164 | `read_block` 的 `old_page` 后备 | **N**（旧终端绑定 `execution_binding.cpp:224-228`；本片默认不绑） |
| 165 | `return direct;` | **W** |
| 166–175 | `write16` 签名＋空指针/门/区间判据 | **W** |
| 176–224 | `write16` 主体：scratch→读密文→CBC IV→ESP 头→ICV→`pipe2`→头部 vmsplice→`file_fd` splice | **W** |
| 225–238 | `helper_write` 后备 | **N**（同 153–164） |
| 239–268 | ICV vmsplice→单报文 splice→`close_pair`→`next_seq+=1`→stats→清零 | **W** |
| 269–295 | `write_block` 多块驱动（校验＋循环＋`PartialWrite`） | **N¹**（新缝一次一格：`seam:48` 单块、`Access::write` 只收 16 否则 `BadWidth`；多块＝调用方逐次发起） |
| 296–301 | `make_file_cache_write_ops` | **N¹**（同 102–114） |
| 302–305 | `zeroize(PageCacheWriteContext&)`＋收尾 | **C**（tear_down 清零；新缝已有 `zeroize_secrets`，`pagecache_seam.hpp:120-129`） |

## 2 计数

- **W 217 · O 12 · C 4 · N 72 · S 0**（合计 305，脚注 A 口径）。
- **S=0 是结论不是省略**：脚本不搬运本件任何**代码**，只给**值**（凭据 `sa`、位置 `offset`）。承载凭据的行按去向归 W/O，另以 `[S]` 标注 8 行：:27 :28 :29 :189 :191 :197 :198 :199。

## 3 脚本输入（ADR-0015）：值面与缺口（切片 2 前必须裁）

- 旧值的唯一合法来源＝运行时 84 字节会话帧：`spi / encap_port / sender_port / icv_len / aes_key / hmac_key`（`session_frame.hpp:13-26`），从不进 profile；新形状＝`SaMaterial`（`access_params.hpp:26-35`）。
- 缺口：① **`icv_len` 无字段**——`:27,29` 与 `esp_compute_icv(:197-199)` 都要它（按 `kEspIcvMaxBytes` 截断 ICV）；② **`encap_port` / `sender_port` 无字段**——O 的门要 `connect_esp_socket`（旧 `execution_binding.cpp:77-106`：bind `sender_port`、connect `encap_port`）；③ `pipe_flags`（旧值＝代码默认 0，全场无其它写入者，`pagecache.hpp:73`）；④ `next_seq` 起值（旧＝1）。
- ⑤ 目标**路径 / fd** 也是值（门件 `doors.md` §归属：二选一须裁）；与本表 204–210 段（不在本件 305 行内）不冲突。

## 4 与已有文档不符 / 需更正的事实

1. **「读不填」只能指公开读面**：`pagecache_seam.hpp:18-19` 写旧树旁边的块读 "not filled here"，但旧 `read_block` 是 `write16:182` 的**内部依赖**（IV＝AES_ECB_DEC(K,旧块)^desired）⇒ 窗口体缺块读则 `land_block` 不成立；不填的只是 `can_read()` / `Access::read`（`kernel_access.hpp:112-117`）。
2. **C 的取材不在本件**：fd 由绑定层开（`execution_binding.cpp:183`，失败自关 `:207-208`）、由 `real_chain_release` 关并清零（`real_ops.cpp:451-488`，`zeroize(ctx->page)` :490）⇒ **C 只有 `:303` 一行来自本件**；新写法「open_doors 开 / close_doors 关（谁开谁关）」与旧规矩同构。
3. **门件两条未量已闭**：`connect_esp_socket` 失败＝返回**负 errno**（`execution_binding.cpp:77-106`，失败路径自关已开的 fd）；`pipe_flags` 旧值＝**代码默认 0** 且无其它写入者 ⇒ 无脚本来源，按 route 内部状态（`-slice1.md` §缝四格）。
4. **耦合数 0 复核**：旧件内 `session::` / `g_exploit` / `g_direct` / `profile::` **0 命中**（与 `-43284.md` §10 一致）。

## 5 门禁

- `make -C core host` = **EXIT <host>**；`make -C core check` = **EXIT <check>**（2026-10-09；本片只写本文件；生产代码与测试未动，`pagecache_chain_test` 断言原样绿）。

## 6 未闭项（不硬凑，交 Lead 裁）

- ① §3 的四个值缺口；② **N¹ 的 46 行**（`:102–114` `:269–295` `:296–301`）**不是**「终端/诊断」而是「被新形状取代 / 无对位」——若口径要求 N 只收终端/诊断行，请把这 46 行改判「无对位·待裁」；③ `write_block` 的多块面是否保留（若保留＝新方法，不进本片窗口体）。

脚注 A：空行与闭合括号随其所属构造计（`:114` 随适配层、`:305` 随 `zeroize` 段）；结构行不影响语义。
