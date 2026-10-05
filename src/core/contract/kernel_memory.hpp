#ifndef GHOSTLOCK_CONTRACT_KERNEL_MEMORY_HPP
#define GHOSTLOCK_CONTRACT_KERNEL_MEMORY_HPP

/* Kernel virtual-address memory capability (alpha batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.5, 3.6, 4.1, 8, 11.
 *
 * KernelMemory is the virtual-first interface a backend implements to expose
 * kernel-VA reads/writes (43499 today; 64560/31431 may reuse the interface, not
 * the mechanism). Mechanism selection is explicit: every primitive takes a
 * MemoryChannel and reports Unsupported rather than silently switching.
 *
 * Contract conventions:
 *   - primitives are pure virtual; read64/write64 are non-virtual (NVI) and
 *     derive from the primitives, so the public API stays stable while an
 *     implementer only implements the primitives;
 *   - update_bits has a default implementation and may be overridden;
 *   - state() is the single source of truth, CapabilityError derives from it.
 *
 * KernelMemoryOps (below) is the C ABIs / plugin-boundary function-pointer
 * bundle. Its definition is moved here verbatim from contract/capabilities.hpp;
 * it is retained because glk_contract_abi.h is a C boundary. R1: this header includes
 * only the standard library and sibling contract headers.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

#include "contract/capability.hpp"

namespace ghostlock::contract {

    /* Mechanism used to reach kernel memory. Unavailable is the explicit "no
     * mechanism chosen / not supported" member. */
    enum class MemoryChannel : std::uint8_t {
        Unavailable = 0,
        Fops,
        PipeBuffer,
        /* A loaded kernel module proxies the primitives through its versioned
         * request channel (delta batch; see contract/abi/glk_contract_abi.h and
         * plugin/kernel_channel.hpp). It is a mechanism, not a separate
         * capability: the same KernelMemory primitives apply. */
        LkmProxy,
    };

    /* Object that carries the primitive. Again Unavailable is explicit. */
    enum class CarrierKind : std::uint8_t {
        Unavailable = 0,
        Ashmem,
        BinderDev,
        LoopControl,
    };

    /* Kernel read/write at a translated virtual address (cve_2026_43499).
     *
     * Migrated verbatim from contract/capabilities.hpp: field names, order and
     * defaults are frozen so existing consumers compile and behave unchanged. */
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

    class KernelMemory : public CapabilityInterface {
    public:
        /* Primitives (pure virtual). The caller picks the mechanism explicitly.
         * read() fills out and returns the number of bytes read; a short read
         * (including zero) is a Faulted error, never a silent success. */
        [[nodiscard]] virtual CapabilityResult<std::uint64_t>
        read(std::uint64_t address, std::span<std::byte> out,
             MemoryChannel channel) noexcept = 0;

        virtual CapabilityStatus
        write(std::uint64_t address, std::span<const std::byte> in,
              MemoryChannel channel) noexcept = 0;

        /* Tier 1 bootstrap: a single-word zero write. */
        virtual CapabilityStatus write_zero(std::uint64_t address) noexcept = 0;

        [[nodiscard]] virtual bool supports(MemoryChannel channel) const noexcept = 0;
        [[nodiscard]] virtual bool supports(CarrierKind carrier) const noexcept = 0;

        /* Per-mechanism lifecycle. state() is authoritative; establish() arms a
         * channel/carrier and close() tears one down (terminal).
         *
         * These three have neutral defaults so a provider that only exposes the
         * Tier 1 primitives (43499's zero_word bootstrap) does not have to spell
         * out a lifecycle it does not have. The defaults are deliberately the
         * fail-closed ones: state() reports NotSupported, establish() returns
         * Unsupported, and close() is a no-op success (nothing was armed). The
         * pure primitives above are untouched by this defaulting. */
        [[nodiscard]] virtual CapabilityState state(MemoryChannel channel) const noexcept {
            (void)channel;
            return CapabilityState::NotSupported;
        }
        [[nodiscard]] virtual CapabilityState state(CarrierKind carrier) const noexcept {
            (void)carrier;
            return CapabilityState::NotSupported;
        }
        virtual CapabilityStatus establish(MemoryChannel channel,
                                           CarrierKind carrier) noexcept {
            (void)channel;
            (void)carrier;
            return std::unexpected(CapabilityError::Unsupported);
        }
        virtual CapabilityStatus close(CarrierKind carrier) noexcept {
            (void)carrier;
            return {};
        }

        /* Convenience (NVI): fixed-width word access derived from the
         * primitives. Not virtual; implementers only implement read/write. */
        [[nodiscard]] CapabilityResult<std::uint64_t>
        read64(std::uint64_t address, MemoryChannel channel) noexcept;

        CapabilityStatus write64(std::uint64_t address, std::uint64_t value,
                                 MemoryChannel channel) noexcept;

        /* Default implementation is a NON-ATOMIC read-modify-write
         * (read -> mask/merge -> write back); a backend with a single-instruction
         * primitive should override it. The default never claims atomicity. */
        virtual CapabilityStatus update_bits(std::uint64_t address,
                                             std::uint64_t mask,
                                             std::uint64_t value,
                                             MemoryChannel channel) noexcept;

        [[nodiscard]] CapabilityKind kind() const noexcept final {
            return CapabilityKind::KernelWrite;
        }

    protected:
        ~KernelMemory() override = default; /* non-owning use: never deleted via base */
    };

    inline CapabilityResult<std::uint64_t>
    KernelMemory::read64(std::uint64_t address, MemoryChannel channel) noexcept {
        std::uint64_t value = 0;
        const std::span<std::byte> out(reinterpret_cast<std::byte *>(&value),
                                       sizeof(value));
        const CapabilityResult<std::uint64_t> read_result = read(address, out, channel);
        if (!read_result.has_value()) {
            return std::unexpected(read_result.error());
        }
        if (read_result.value() != out.size()) {
            return std::unexpected(CapabilityError::Faulted);
        }
        return value;
    }

    inline CapabilityStatus
    KernelMemory::write64(std::uint64_t address, std::uint64_t value,
                          MemoryChannel channel) noexcept {
        const std::span<const std::byte> in(reinterpret_cast<const std::byte *>(&value),
                                            sizeof(value));
        return write(address, in, channel);
    }

    inline CapabilityStatus
    KernelMemory::update_bits(std::uint64_t address, std::uint64_t mask,
                              std::uint64_t value, MemoryChannel channel) noexcept {
        const CapabilityResult<std::uint64_t> current = read64(address, channel);
        if (!current.has_value()) {
            return std::unexpected(current.error());
        }
        const std::uint64_t next = (current.value() & ~mask) | (value & mask);
        return write64(address, next, channel);
    }

} // namespace ghostlock::contract

#endif
