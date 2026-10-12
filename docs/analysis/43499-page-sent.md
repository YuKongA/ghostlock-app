# 43499 · 页面已进内核（round 86-87）与下一个二分实验

## 本轮里程碑（真机逐字）

```
main: page sent value=12672 errno=0 bytes=12672   ← 12 672 B 经 shaping 对写进内核（Unix socket ⇒ 内核缓冲）
main: route status=0/0 attempts=2 calls=1
main: victim uid=2000                            ← 仍未 root ✗
```

⇒ "**页面从未进入内核**"（本树自承认的 `NOT YET IN THE SEQUENCE`）**已修**：

1. `PrepareOutcome`/`PrepareReport` 交出 `shaping_fd` 与对端 `shaping_peer_fd`（只留一端 ⇒ `send` 撞 SIGPIPE，
   真机量到 RC=141 ⇒ 两头都留，注释在码里）；
2. main：页面**建在本地缓冲的 `payload_delta` 处**（指针与实际字节位置一致）；
3. main：`payload_base = prepared.base + payload.delta`（**来自泄漏**，不再用堆的 alias_delta）；
4. main：`send_bytes(shaping_fd, page_image.data(), size)` ⇒ 进内核 ⇒ 之后关闭两端。

## 下一个二分实验（规格，已可执行）

**目的**：切开两种可能 —— ① 机制（内核走链）根本没写；② 写了但没落到受害者地址。

**做法**：把写目标临时指向**我们自己发出去的那块缓冲之内**（`payload_base + 0x2000`），发完之后从对端
`shaping_peer_fd` `recv` 回同一块内存（内核队列里的就是那份字节），检查 `+0x2000` 处是否被改：

- **被改** ⇒ 机制在写 ⇒ 问题只剩"地址/对位"；
- **没改** ⇒ 机制没触发 ⇒ 问题在"走链"。

**注意**：对端必须在 `recv` 之后才关（先关会 SIGPIPE，已量到）。

## 门禁

- android → RC=0，0 error / 0 warning；check → RC=0，108 PASS；lint → 0。
