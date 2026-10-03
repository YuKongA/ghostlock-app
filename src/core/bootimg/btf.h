#ifndef GHOSTLOCK_BOOTIMG_BTF_H
#define GHOSTLOCK_BOOTIMG_BTF_H

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ghostlock::bootimg {

    /* Decode the kernel's embedded BTF blob and find
     *
     *   offsetof(struct tracepoint, funcs)
     *
     * The BTF format is documented in
     * kernel/btf.c and the pahole documentation.  For arm64 kernels the
     * struct has:
     *
     *   char *name;            // +0x00
     *   const char *key;       // +0x08
     *   ... static_call_key, static_call_tramp, iterator
     *   void *probestub;       // +0x30 (when HAVE_STATIC_CALL)
     *   ... regfunc, unregfunc
     *   struct tracepoint_func *funcs;  // +0x48
     */
    class BtfReader final {
    public:
        explicit BtfReader(std::span<const std::uint8_t> data) noexcept;

        /* Find `struct tracepoint` and return the byte offset of its
         * `funcs` member.  Returns std::nullopt when the struct is not
         * present or the BTF blob is malformed. */
        [[nodiscard]] std::optional<std::uint32_t> tracepoint_funcs_offset() const;

        [[nodiscard]] bool valid() const noexcept { return valid_; }

    private:
        std::span<const std::uint8_t> data_;
        const std::uint8_t *base_ = nullptr;
        bool valid_ = false;
    };

} // namespace ghostlock::bootimg

#endif // GHOSTLOCK_BOOTIMG_BTF_H
