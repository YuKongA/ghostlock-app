#ifndef GHOSTLOCK_CONTRACT_CAPABILITIES_HPP
#define GHOSTLOCK_CONTRACT_CAPABILITIES_HPP

#include <cstddef>
#include <cstdint>

namespace ghostlock::contract {
    /* Capability handles (ADR-0004 T1): trivially copyable, standard-layout and
     * noexcept; availability is derived from the handle itself, never a separate
     * bool flag. They are the neutral contract between a backend's primitive and
     * the steps/terminal that consume it, so a backend can offer one of several
     * primitive families without the framework assuming kernel memory. */

    /* Kernel read/write at a translated virtual address (cve_2026_43499). */
    struct KernelMemoryOps final {
        void *ctx = nullptr;
        std::int32_t (*read)(void *ctx, std::uint64_t address, void *out,
                             std::size_t size) noexcept = nullptr;
        std::int32_t (*write)(void *ctx, std::uint64_t address, const void *in,
                              std::size_t size) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return read != nullptr && write != nullptr;
        }
    };

    /* Arbitrary 16-byte file page-cache write (cve_2026_43284; 43503 reuse).
     * `write16` stores exactly 16 bytes at a file offset through the ESP/CBC-IV
     * primitive; no kernel address or KASLR knowledge is involved. */
    struct FileCacheWriteOps final {
        void *ctx = nullptr;
        std::int32_t (*write16)(void *ctx, std::uint64_t file_offset,
                                const void *bytes16) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept { return write16 != nullptr; }
    };
} // namespace ghostlock::contract

#endif
