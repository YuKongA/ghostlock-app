# 附：CBC 判据与 syscall 面规格

> 本件是 `seam-port-design-43499-aes.md` 的附件 · 2026-10-09

## 23 CBC 那一件的验收判据（已独立算出，供下一件用）

与 AES/HMAC 同一做法：**先算再说** ✓ 用本机 OpenSSL 算 ✓ 并与 NIST 的原值核对 ✓。

```
key = 603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4
iv  = 000102030405060708090a0b0c0d0e0f
PT1 = 6bc1bee22e409f96e93d7e117393172a
CT1 = f58c4c04d6e5f1ba779eabfb5f7bfbd6      ← 与 NIST SP 800-38A F.2.5 / F.2.6 原值一致 ✓
PT2 = ae2d8a571e03ac9c9eb76fac45af8e51
CT(2 blocks) = f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d
                └── 第二块 9cfc4e967edb808d679f777bc6702c7d ⇒ 链式关系被验过 ✓
```

⇒ 下一件（CBC）的判据 = **单块向量 + 双块链式向量 + 反向解密 + 非块长拒绝** ✓（后一条正是 ECB 那件**做不到**而 CBC **能做**的 ✓ —— 这也是当初把判据拆分的原因 ✓）。

**状态说明**：写体的 `esp_icv` 已接上真 HMAC ✓（`ef2f27f8` ✓），判据是我算的 `198a607ebc…` ✓ ⇒ **43284 的写四段全真** ✓。HMAC 那件里「>128 字节走临时 vector」的收拢（改为**上界具名拒绝** ✓）正在做 ✓ ⇒ 树里那条路径**今天不可达**（写体只传 16 字节 ✓）⇒ 属**潜在**违规 ✓ 而非活跃 ✓。

## 24 下一件（真实 syscall 面）的规格：旧树本来就有这道缝

实测旧 `src/core/backend/cve_2026_43284/pagecache/pagecache.cpp` 的**文件头自述**：

> The direct-file path of exp.c `patch_file_cbc`: **read the old block, derive the CBC IV, frame one
> ESP datagram into a pipe and splice that pipe to the ESP socket. All syscalls go through
> `pagecache::SpliceIoOps`.**

⇒ 三条直接可用的结论：

1. **旧树本来就有这道缝** ✓ 名字是 `pagecache::SpliceIoOps` ✓ ⇒ 我新树里的 `seam::SpliceOps` **不是新发明** ✓ 而是同一设计的后代 ✓ ⇒ 移植时**应沿用它的方法集**（而不是我凭需要拼一个 ✓）；
2. **原语是 `vmsplice`** ✓（不是 `write` ✓）：实测 `io.vmsplice(fd, data + done, remaining, 0U)` ✓ ⇒ 且**短传输要重试**（`data + done` / `remaining` ✓）、**`-EINTR` 要单独处理** ✓ ⇒ 这两条是**实测出来的行为** ✓，不是我加的 ✓；
3. **就绪前提**（实测 `page_cache_ready(ctx)` ✓）：`ctx.io.available()` ✓ · `socket_fd >= 0` ✓ · `icv_len != 0` ✓ · 且 **`file_fd >= 0` 或 `helper_write.available()`** ✓（两路来源：App 打不开厂商文件时走**注入的 crash_dump helper 写源** ✓）。

**另记（路径教训）**：旧件在 **`pagecache/` 子目录**下（`pagecache/pagecache.cpp` ✓），我第一次按 `backend/cve_2026_43284/pagecache.cpp` 找 ⇒ **没命中** ✗ ⇒ 正确做法是**先 `git ls-tree` 拿文件清单再逐件读** ✓ —— 这条已在本节前半段实际用过一次（先列清单才发现 `ipsec/` `lkm/` `steps/` `embed/` 四个子目录 ✓）。

## 25 缺陷（Lead 自查发现 · 未修）：写体的报文格式与旧树不一致

我在 §24 里把「核对报文布局」认领给自己 ✓ ⇒ 本轮核完 ⇒ **不一致** ✗ ⇒ 这是**本批最重要的发现** ✓。

### 旧树（实测 `pagecache.hpp` 文件头）

```
SPI(4) | Seq(4) | IV(16) | ciphertext(16) | ICV(icv_len)
⇒ 总长 = 40 + icv_len（icv_len=12 时 = 52 字节）
```

### 我写体的现状（实测 `seam/pagecache_write.hpp:108-117`）

```cpp
std::uint8_t datagram[kBlockBytes] = {};        // ← 只有 16 字节 ✗
for (i...) datagram[i] = block[i];              // ← 明文块先铺满 16 字节
store_be32(datagram,     spi_);                 // ← 又覆写前 4 字节
store_be32(datagram + 4, next_seq_);            // ← 再覆写 4 字节
splice_.compute_icv(datagram, kBlockBytes, ...) // ← 只对 16 字节算 ICV ✗
splice_.splice_one(..., datagram, kBlockBytes, position);  // ← 只发 16 字节 ✗
```

### 逐条差异（五处，都是硬差异）

1. **长度**：我发 16 字节 ✗；旧发 `40 + icv_len` ✓；
2. **IV 算了但从未放进报文** ✗（`iv` 是局部变量，算完即丢弃 ✓）；
3. **密文与头混在一起** ✗：我先把块铺满，再用头覆写前 8 字节 ⇒ 第 8–15 字节仍是**明文块**的残段 ✓ ⇒ 整包**不是合法 ESP** ✗；
4. **ICV 只对 16 字节算** ✗（旧对**整个报文**算 ✓）；
5. **ICV 没有附在报文尾部** ✗（`icv` 同样是算完即丢 ✓）。

### 为什么测试没抓住它 ✗

`pagecache_write` 的测试用**记录用 fake** ✓，但它只断言了**头的两段**（`spi_in_header` ✓ `sequence_in_header` ✓ = 字节 0–7 ✓）⇒ **对第 8 字节之后与总长度没有断言** ✗ ⇒ ⇒ 于是它**通过**了 ✓。⇒ 这正好印证本仓那条规矩：**断言要证明它能失败** ✓ —— 这条测试**抓不住**格式错误 ✓，因为它的断言面**比错误面窄** ✗。

### 处置（下一步，立即）

把 `write_block` 的组装改成旧布局 ✓：`datagram[40 + icv_len]` · 先写 SPI/Seq ✓ · 再把 **IV 写入偏移 8** ✓ · 密文写偏移 24 ✓ · 对**整个 40 字节**算 ICV ✓ · 把 ICV 附在偏移 40 ✓ · 发送 **40+icv_len** 字节 ✓；并**把测试的断言扩展到字节 8 之后与总长度** ✓（否则修了也无人守 ✓）。

**尚未修** ✓：本轮只做了核对与记录 ✓ ⇒ 修改留到下一轮（避免交出半成品 ✓）。

## 26 确认：线上那 16 字节密文来自被 splice 的页，不来自缓冲区（作者复核）

`real_splice_io` 的作者**对着旧件逐字节核过**并给出契约（我已确认）：

```
① vmsplice  [0,24)    = SPI(4) | Seq(4) | IV(16)
② splice    页 16 B   = file_fd@offset（无 fd 时走 crash_dump helper）
③ vmsplice  [40,size) = ICV
④ splice    pipe→socket，共 size 字节
```

**要点**：`[24,40)` 那段在内存缓冲区里**只用于算 ICV** ⇒ **线上那 16 字节是被 `splice` 的页本身** ⇒ 这正是 `splice` 的**零拷贝**意义 ⇒ 实现**必须**保持这个分工（若改成从缓冲区发那 16 字节 ⇒ 丢掉 splice 的全部价值）。

**为什么值得单独记一条**：这是**读代码时最容易看错**的地方 —— 我在 §25 修缺陷时差点按「全从缓冲区发」去改；正确读法是「缓冲区里的密文槽**只为 ICV 存在**」。两份独立核对（作者对旧件 · 我对旧注释）现在一致。

**旧偏移（作者实测 `ipsec.hpp`）**：SPI@0 · Seq@4 · IV@8 · ciphertext@24 · ICV@40 ⇒ 与我写体现在的组装**逐字节一致**。

**两条具名拒绝**（确认）：`size < 40`（frame 放不下页）· `fd < 0` 且无 helper。**管道→socket 短传输不重试**（旧规：不能续写同一报文）。

**其余实测（作者报告，与我的复核相符）**：证伪（把「页 16 字节」改成 17 ⇒ `FAIL: and it is the sixteen-byte block` + 退出 1 + 零残留）；测试覆盖短传输重试（含**重试按实际搬运前进偏移**）· `-EINTR` · 三路来源（direct/bridge/读回退）· 桥接短或错 ⇒ **socket splice 调用 0 次** · 无 fd 无 helper 连管道都不开 · `send_datagram` 槽位在 splice 路径**不被使用**。

**与旧件不符（3 处，作者自报）**：① 路径与命名空间 `pagecache/splice_io` → `seam/real_splice_io`，编排收进类方法（因为新缝是 `SpliceOps` 虚类）；② 旧 `send_datagram` 实现用 `::send`，新按六调用用 **`::sendmsg`**（槽位保留但不使用）；③ 全程无分配 ⇒ 无需上界拒绝。
