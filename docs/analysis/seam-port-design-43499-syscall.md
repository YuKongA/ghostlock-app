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
