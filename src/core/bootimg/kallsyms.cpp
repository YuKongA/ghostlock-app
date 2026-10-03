#include "bootimg/kallsyms.h"

#include <cstdint>
#include <cstring>

namespace ghostlock::bootimg {

    static uint32_t rd32(const uint8_t *p) noexcept {
        uint32_t v;
        std::memcpy(&v, p, sizeof v);
        return v;
    }

    static std::optional<std::string_view> read_name_from_table(
        const uint8_t *table, std::uint32_t table_size, std::uint32_t offset) {
        if (offset >= table_size) {
            return std::nullopt;
        }
        if (table[offset] == 0) {
            return std::nullopt;
        }
        std::size_t len = 0;
        while (offset + len < table_size && table[offset + len] != 0) {
            ++len;
        }
        return std::string_view(reinterpret_cast<const char *>(table + offset),
                                len);
    }

    KallsymsReader::KallsymsReader(std::span<const std::uint8_t> data) noexcept
        : data_(data) {
        if (data.size() < 0x14) {
            return;
        }
        const auto *p = data.data();
        num_symbols_ = rd32(p + 0x00);
        num_tokens_ = rd32(p + 0x04);
        num_names_ = rd32(p + 0x08);
        name_table_size_ = rd32(p + 0x0c);
        token_table_size_ = rd32(p + 0x10);
        base_ = p;

        /* Sanity checks: the name table must be non-trivial and
         * symbol offsets must be in range. */
        if (num_names_ == 0 || name_table_size_ == 0 || num_symbols_ == 0) {
            return;
        }

        /*
         * Layout of the token table (kernel/kallsyms.c):
         *   offset 0x00     num_syms
         *   offset 0x04     num_tokens
         *   offset 0x08     num_names
         *   offset 0x0c     name_table_size
         *   offset 0x10     token_table_size
         *   offset 0x14     token_table[num_tokens]
         *   ...
         *   symbol_offsets[num_syms]        (u32, offset into name_table)
         *   names[num_names]                (null-terminated strings)
         *   tokens[num_syms]                (u32)
         *   symbols[num_syms]               (u64, absolute addresses)
         */
        std::size_t off = 0x14 + token_table_size_ * sizeof(uint32_t);
        if (off + num_symbols_ * 4 > data.size()) {
            return;
        }
        /* names table follows symbol_offsets */
        std::size_t names_start = off;
        if (names_start + name_table_size_ > data_.size()) {
            return;
        }
        off = names_start + name_table_size_;

        /* tokens[num_syms] */
        off += name_table_size_;
        if (off + num_symbols_ * 4 > data.size()) {
            return;
        }
        off += num_symbols_ * 4;

        /* symbols[num_syms] at the end */
        off += num_symbols_ * 4;
        std::uint64_t last_sym = 0;
        for (std::uint32_t i = 0; i < num_symbols_; ++i) {
            std::uint64_t addr = 0;
            std::memcpy(&addr, base_ + off + i * 8, sizeof addr);
            if (addr > last_sym) {
                last_sym = addr;
            }
        }
        valid_ = (last_sym > 0) &&
                 (last_sym >= static_cast<uint64_t>(num_symbols_));
    }

    std::optional<uint64_t>
    KallsymsReader::find_symbol(std::string_view name) const {
        if (!valid_ || !base_) {
            return std::nullopt;
        }

        std::size_t off = 0x14 + token_table_size_ * sizeof(uint32_t);
        off += num_symbols_ * 4;  /* symbol_offsets */
        off += name_table_size_;

        /* tokens[num_syms] */
        off += num_symbols_ * 4;
        std::span<const uint64_t> symbols(
            reinterpret_cast<const uint64_t *>(base_ + off), num_symbols_);

        std::span<const uint8_t> names(base_, name_table_size_);
        for (std::uint32_t i = 0; i < num_symbols_; ++i) {
            std::uint32_t name_off = rd32(base_ +
                                          (0x14 + token_table_size_ * 4) +
                                          i * 4);
            auto nv = read_name_from_table(names.data(), name_table_size_,
                                           name_off);
            if (!nv || nv->size() != name.size()) {
                continue;
            }
            if (std::memcmp(nv->data(), name.data(), name.size()) != 0) {
                continue;
            }
            return static_cast<uint64_t>(symbols[i]);
        }
        return std::nullopt;
    }

} // namespace ghostlock::bootimg
