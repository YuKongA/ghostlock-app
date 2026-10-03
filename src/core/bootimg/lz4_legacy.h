#ifndef GHOSTLOCK_LZ4_LEGACY_H
#define GHOSTLOCK_LZ4_LEGACY_H

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace ghostlock::bootimg {

    /** LZ4-legacy 帧识别：魔数 0x184C2102（字节序 02 21 4C 18）。 */
    [[nodiscard]] inline bool lz4_is_legacy(std::span<const std::uint8_t> d, std::size_t off = 0) {
        if (off + 4 > d.size()) return false;
        return d[off] == 0x02 && d[off+1] == 0x21 && d[off+2] == 0x4C && d[off+3] == 0x18;
    }

    /**
     * LZ4 单块解压（标准 LZ4 block sequence）。
     * 返回解出的字节数；出错返回 false 并清空 out。
     */
    [[nodiscard]] inline bool lz4_decompress_block(
        const std::uint8_t* src, std::size_t src_len,
        std::vector<std::uint8_t>& out, std::size_t max_out)
    {
        std::size_t ip = 0, op = out.size();
        while (true) {
            if (ip >= src_len) return false;
            const std::uint8_t token = src[ip++];
            // literals
            std::size_t lit_len = token >> 4;
            if (lit_len == 15) {
                std::uint8_t b;
                do {
                    if (ip >= src_len) return false;
                    b = src[ip++];
                    lit_len += b;
                } while (b == 255);
            }
            if (ip + lit_len > src_len) return false;
            if (op + lit_len > max_out) return false;
            /* The destination starts empty, so the buffer has to be grown before
             * it is written through: memcpy into data()+op of a short vector
             * writes past its size. */
            out.resize(op + lit_len);
            std::memcpy(out.data() + op, src + ip, lit_len);
            ip += lit_len; op += lit_len;

            if (ip >= src_len) break; // 最后一节没有 match
            // match
            if (ip + 2 > src_len) return false;
            const std::size_t match_off =
                static_cast<std::size_t>(src[ip]) |
                (static_cast<std::size_t>(src[ip + 1]) << 8);
            ip += 2;
            if (match_off == 0 || match_off > op) return false;
            std::size_t match_len = (token & 0xF) + 4;
            if ((token & 0xF) == 15) {
                std::uint8_t b;
                do {
                    if (ip >= src_len) return false;
                    b = src[ip++];
                    match_len += b;
                } while (b == 255);
            }
            if (op + match_len > max_out) return false;
            const std::size_t match_start = op - match_off;
            out.resize(op + match_len);
            /* Byte at a time on purpose: LZ4 allows the match to overlap the
             * bytes just produced, which is how runs are encoded. */
            for (std::size_t k = 0; k < match_len; ++k) {
                out[op + k] = out[match_start + k];
            }
            op += match_len;
        }
        return true;
    }

    /**
     * 解压完整 LZ4-legacy 流（多帧拼接是 Android vendor_ramdisk 的常态）。
     * 帧：magic(4) + N * [block_size(4,LE) + data(block_size)] + 终止 0x00000000。
     */
    [[nodiscard]] inline std::vector<std::uint8_t> lz4_decompress_legacy(
        std::span<const std::uint8_t> d, std::size_t max_output)
    {
        std::vector<std::uint8_t> out;
        std::size_t off = 0;
        while (off + 8 <= d.size()) {
            if (!lz4_is_legacy(d, off)) break;
            off += 4;
            while (true) {
                if (off + 4 > d.size()) return {};
                std::uint32_t bs;
                std::memcpy(&bs, d.data() + off, 4);
                off += 4;
                if (bs == 0) break;           // 帧终止
                if (bs > (8u << 20)) return {}; // 单块上限 8MB，防御坏数据
                if (off + bs > d.size()) return {};
                if (out.size() > max_output) return {};
                if (!lz4_decompress_block(d.data() + off, bs, out, max_output)) return {};
                off += bs;
            }
        }
        return out;
    }

} // namespace ghostlock::bootimg
#endif
