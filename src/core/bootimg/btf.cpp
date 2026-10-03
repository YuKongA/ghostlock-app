#include "bootimg/btf.h"

#include <cstring>

namespace ghostlock::bootimg {

    static uint32_t rd32(const uint8_t *p) noexcept {
        uint32_t v;
        std::memcpy(&v, p, sizeof v);
        return v;
    }

    /* BTF format (kernel/btf.c):
     *
     * Header (24 bytes):
     *   +0x00  uint32_t  magic      = 0xeB9F
     *   +0x04  uint8_t   version    = 1
     *   +0x05  uint8_t   flags
     *   +0x06  uint8_t   hdr_size   = 24
     *   +0x08  uint32_t  num_info
     *   +0x0c  uint32_t  num_strs
     *
     * Info section: 12 bytes per entry
     *   +0x00  uint32_t  name_off   (offset into string table)
     *   +0x04  uint32_t  type       = (vlen << 24) | (kind << 16) | kindflag
     *   +0x08  uint32_t  size_or_type
     *
     * String table: num_strs null-terminated bytes.
     *
     * Kind: 3 = STRUCT, 4 = UNION.
     *
     * STRUCT/UNION members: 12 bytes each
     *   +0x00  uint32_t  name_off
     *   +0x04  uint32_t  type
     *   +0x08  uint32_t  bit_offset (bit offset of the member)
     */
    BtfReader::BtfReader(std::span<const std::uint8_t> data) noexcept
        : data_(data) {
        if (data.size() < 24) {
            return;
        }
        base_ = data.data();
        if (rd32(base_ + 0x00) != 0xeB9F) {
            return;
        }
        if (base_[4] != 1) {
            return;
        }
        std::uint32_t num_info = rd32(base_ + 8);
        std::uint32_t num_strs = rd32(base_ + 12);
        if (num_info == 0 || num_strs == 0) {
            return;
        }
        std::size_t hdr = 24;
        std::size_t info_end = hdr + static_cast<std::size_t>(num_info) * 12;
        if (info_end + num_strs > data.size()) {
            return;
        }
        valid_ = true;
    }

    std::optional<std::uint32_t>
    BtfReader::tracepoint_funcs_offset() const {
        if (!valid_ || !base_) {
            return std::nullopt;
        }

        std::uint32_t num_info = rd32(base_ + 8);
        std::uint32_t num_strs = rd32(base_ + 12);
        const std::uint8_t *strings = base_ + 24 + num_info * 12;
        const std::uint8_t *info = base_ + 24;

        for (std::uint32_t i = 1; i < num_info; ++i) {  /* 0 = void */
            const std::uint8_t *ent = info + i * 12;
            std::uint32_t name_off = rd32(ent + 0);
            std::uint32_t type = rd32(ent + 4);
            std::uint32_t kind = (type >> 16) & 0x1F;
            std::uint32_t vlen = type & 0xFFFF;

            /* Only STRUCT or UNION */
            if (kind != 3 && kind != 4) {
                continue;
            }

            if (name_off >= num_strs) {
                continue;
            }
            std::string_view name(
                reinterpret_cast<const char *>(strings + name_off));
            if (name != "tracepoint") {
                continue;
            }

            const std::uint8_t *memb = ent + 12;
            for (std::uint32_t m = 0; m < vlen; ++m) {
                const std::uint8_t *me = memb + m * 12;
                std::uint32_t mname_off = rd32(me + 0);
                if (mname_off >= num_strs) {
                    continue;
                }
                std::string_view mname(
                    reinterpret_cast<const char *>(strings + mname_off));
                if (mname == "funcs") {
                    /* bit_offset is in bits; the member itself is 8 bytes */
                    std::uint32_t mbit_off = rd32(me + 8);
                    std::uint32_t byte_off = mbit_off / 8;
                    return byte_off;
                }
            }
        }
        return std::nullopt;
    }

} // namespace ghostlock::bootimg
