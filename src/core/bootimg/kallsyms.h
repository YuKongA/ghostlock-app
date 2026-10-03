#ifndef GHOSTLOCK_BOOTIMG_KALLSYMS_H
#define GHOSTLOCK_BOOTIMG_KALLSYMS_H

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ghostlock::bootimg {

    /* Decode the kernel's embedded kallsyms token table.
     *
     * Layout (kernel/kallsyms.c):
     *   +0x00  uint32_t  num_symbols
     *   +0x04  uint32_t  num_tokens
     *   +0x08  uint32_t  num_names
     *   +0x0c  uint32_t  name_table_size
     *   +0x10  uint32_t  token_table_size
     *   +0x14  uint32_t  token_table[num_tokens]
     *   +0x??  uint32_t  symbol_offsets[num_symbols]
     *   +0x??  uint8_t   names[name_table_size]
     *   +0x??  uint32_t  tokens[num_symbols]
     *   +0x??  uint64_t  symbols[num_symbols]
     *
     * We only need: symbol(name) -> absolute address for:
     *   __tracepoint_sys_exit
     *   __tracepoint_android_rvh_commit_creds
     *   __probestub_sys_exit
     */
    class KallsymsReader final {
    public:
        explicit KallsymsReader(std::span<const std::uint8_t> data) noexcept;

        [[nodiscard]] std::optional<uint64_t> find_symbol(
            std::string_view name) const;

        [[nodiscard]] bool valid() const noexcept { return valid_; }

    private:
        std::uint32_t num_symbols_ = 0;
        std::uint32_t num_tokens_ = 0;
        std::uint32_t num_names_ = 0;
        std::uint32_t name_table_size_ = 0;
        std::uint32_t token_table_size_ = 0;

        const std::uint8_t *base_ = nullptr;
        std::span<const std::uint8_t> data_;
        bool valid_ = false;
    };

} // namespace ghostlock::bootimg

#endif // GHOSTLOCK_BOOTIMG_KALLSYMS_H
