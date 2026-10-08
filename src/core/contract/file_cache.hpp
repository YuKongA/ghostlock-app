#ifndef GHOSTLOCK_CONTRACT_FILE_CACHE_HPP
#define GHOSTLOCK_CONTRACT_FILE_CACHE_HPP

/* File page-cache write capability (alpha batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.5, 4.2, 8, 11.
 *
 * FileCacheWrite is the first genuinely cross-backend capability: 43284 writes
 * 16 bytes at an arbitrary file offset through the ESP/CBC-IV primitive and
 * 43503 exposes the same page-cache domain. Per ruling 8 the read half of the
 * read-modify-write protocol lives on this interface too (read16), because a
 * partial block write has the same lifecycle as the write.
 *
 * FileCacheWriteOps (below) is the C ABIs / plugin-boundary function-pointer
 * bundle. Its definition is moved here verbatim from contract/capabilities.hpp
 * and retained because glk_contract_abi.h is a C boundary. R1: this header includes
 * only the standard library and sibling contract headers.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "contract/capability.hpp"

namespace ghostlock::contract {

    /* Arbitrary 16-byte file page-cache write (cve_2026_43284; 43503 reuse).
     * `write16` stores exactly 16 bytes at a file offset through the ESP/CBC-IV
     * primitive; no kernel address or KASLR knowledge is involved.
     *
     * Migrated verbatim from contract/capabilities.hpp: field names, order and
     * defaults are frozen so existing consumers compile and behave unchanged. */
    struct FileCacheWriteOps final {
        void *ctx = nullptr;
        std::int32_t (*write16)(void *ctx, std::uint64_t file_offset,
                                const void *bytes16) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept { return write16 != nullptr; }
    };

    class FileCacheWrite : public CapabilityInterface {
    public:
        /* Read the current 16-byte block so a partial write can be merged. */
        [[nodiscard]] virtual CapabilityStatus
        read16(std::uint64_t file_offset, std::span<std::byte, 16> out) noexcept = 0;

        /* Store exactly 16 bytes at a file offset. */
        virtual CapabilityStatus
        write16(std::uint64_t file_offset,
                std::span<const std::byte, 16> in) noexcept = 0;

        /* Convenience: split an arbitrary byte range into 16-byte writes. The
         * final partial block is read-modify-written through read16, so it needs
         * the same lifecycle as write16 (hence the design decision to keep the
         * read half on this interface). */
        CapabilityStatus write_bytes(std::uint64_t file_offset,
                                     std::span<const std::byte> bytes) noexcept;

        [[nodiscard]] CapabilityKind kind() const noexcept final {
            return CapabilityKind::FileCacheWrite;
        }

    protected:
        ~FileCacheWrite() override = default; /* non-owning use: never deleted via base */
    };

    inline CapabilityStatus
    FileCacheWrite::write_bytes(std::uint64_t file_offset,
                                std::span<const std::byte> bytes) noexcept {
        constexpr std::size_t kBlock = 16;
        std::size_t offset = 0;
        while ((bytes.size() - offset) >= kBlock) {
            const std::span<const std::byte, kBlock> block(bytes.subspan(offset, kBlock));
            const CapabilityStatus status =
                    write16(file_offset + static_cast<std::uint64_t>(offset), block);
            if (!status.has_value()) {
                return status;
            }
            offset += kBlock;
        }
        if (offset < bytes.size()) {
            std::array<std::byte, kBlock> tail{};
            const std::size_t remainder = bytes.size() - offset;
            for (std::size_t i = 0; i < remainder; ++i) {
                tail[i] = bytes[offset + i];
            }
            const std::span<std::byte, kBlock> tail_span(tail);
            const CapabilityStatus read_status =
                    read16(file_offset + static_cast<std::uint64_t>(offset), tail_span);
            if (!read_status.has_value()) {
                return read_status;
            }
            const std::span<const std::byte, kBlock> tail_out(tail);
            return write16(file_offset + static_cast<std::uint64_t>(offset), tail_out);
        }
        return {};
    }

} // namespace ghostlock::contract

#endif
