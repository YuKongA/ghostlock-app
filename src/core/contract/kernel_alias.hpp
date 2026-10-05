#ifndef GHOSTLOCK_CONTRACT_KERNEL_ALIAS_HPP
#define GHOSTLOCK_CONTRACT_KERNEL_ALIAS_HPP

/* Image-address to direct-map translation capability (alpha batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.5, 4.2, 8, 11.
 *
 * KernelAlias is deliberately separate from KernelMemory: it translates a
 * kernel-image relative address (pre-slide) into a direct-map virtual address,
 * so a caller cannot accidentally pass a file offset or an untranslated image
 * address to a memory primitive (section 3.5 address-kind split). 43499
 * provides it for the vivo countermeasure path; a backend without the
 * mechanism returns Unsupported rather than guessing.
 *
 * R1: only the standard library and sibling contract headers.
 */

#include <cstdint>

#include "contract/capability.hpp"

namespace ghostlock::contract {

    class KernelAlias : public CapabilityInterface {
    public:
        /* Returns the direct-map VA for image_addr, or a typed error when the
         * translation is not possible (never 0-as-success). */
        [[nodiscard]] virtual CapabilityResult<std::uint64_t>
        to_direct_map(std::uint64_t image_addr) const noexcept = 0;

        [[nodiscard]] CapabilityKind kind() const noexcept final {
            return CapabilityKind::KernelAlias;
        }

    protected:
        ~KernelAlias() override = default; /* non-owning use: never deleted via base */
    };

} // namespace ghostlock::contract

#endif
