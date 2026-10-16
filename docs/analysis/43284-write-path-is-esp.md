# 写路径就是 ESP 报文本身（2026-10-15 读 real_splice_io.hpp 的结论）

## RealSpliceIo 不是「直接写页缓存」，它是 ESP 写体的 I/O 面

```cpp
class RealSpliceIo final : public SpliceOps {
    RealSpliceIo(SpliceIoOps io, int file_fd, int pipe_flags = 0,
                 OldPageSource old_page = {}, HelperWriteSource helper = {}) noexcept;

    long read_at(int fd, std::uint8_t *out, std::size_t size, std::uint64_t offset);      // 读（经 surface）
    bool splice_one(int socket_fd, const std::uint8_t *datagram,
                    std::size_t size, std::uint64_t offset);                             // **发一个报文**
    void compute_iv (old_block, desired_block, key, out);                                // IV（CBC）
    void compute_icv(datagram, size, hmac_key, icv_len, out);                            // ICV（HMAC）
    bool can_serve_page() const { return file_fd_ >= 0 || helper_.available(); }
    bool can_read_page()  const { return file_fd_ >= 0 || old_page_.available(); }
    int  last_error() const;                                                             // 0 / 前置检查序号 / -errno
};
```

`splice_one` 的注释写得很清楚：**「发一个 size 字节的报文：内存里的前半段 + 从目标 `offset` splice 出来的那一页 + 内存里的后半段」**。
也就是说：

1. 写进目标文件的动作**发生在发送 ESP 报文的路径上** —— 报文带密码学（`compute_iv`/`compute_icv`），由内核 xfrm/SA 处理；
2. 所以 `AccessParams.sa`（SPI/密钥/端口）**不是可选的前提，而是写机制本身的一部分**；
3. 我早先用 `landBlock`（落地对象那条路）写不进去，正是因为那条路**没有这一段 ESP 报文腿** —— 现象与机制现在完全对上了；
4. `OldPageSource.read16`（读旧页）与 `HelperWriteSource.splice16`（让 helper 替我们 splice）就是「谁能提供那一页」的两个来源；
   `can_serve_page/can_read_page` 就是在问这件事。

## 对计划的影响（重要）

- 「先把 carrier 探好再谈 SA」这个次序**不成立**：SA 就是写的前提；
- 因此下一步必须先把 **内核里那条 SA** 解决（§11.33 的问题）—— 它是写路径的一部分，不是外部增强；
- 退役树那次 PASS 的 SA 材料来自**帧**（工具里 `phase_credentials` 读它），也就是说：要么帧里带着可用的 SA 材料、
  要么由别的步骤把它装进内核；这一点要在移植时确认（读工具里 `phase_credentials` 与帧的来源）。

## 下一步（修正后的次序）

1. 读工具的 `phase_credentials`：SA 材料从哪来（帧路径、谁产出）；
2. 读工具里 `splice_one` 与 socket 的绑定方式（它怎么拿到 socket_fd、目标 SPI 如何一致）；
3. 据此决定：我们把哪一段搬进 Kotlin、哪一段必须保持 native 薄原语；
4. 之后才是 patch#1 的真机复跑（判据不变：crash_dump64 的 offset 16 窗口与源字节一致）。
