#ifndef GHOSTLOCK_CONTRACT_CHILD_TASK_HPP
#define GHOSTLOCK_CONTRACT_CHILD_TASK_HPP

/* Root-child task access capability (alpha batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.5, 4.2, 8, 11.
 *
 * ChildTask hands back the current root-child task handle. It is refreshed on
 * every call and never cached: the child (and therefore its task pointer) can
 * change during the chain, so a stale cached value would be a correctness bug.
 * The handle is a kernel task pointer, exposed as an opaque u64 to match the
 * plugin ABI; 0 is not a valid handle and must be an error, not success.
 *
 * R1: only the standard library and sibling contract headers.
 */

#include <cstdint>

#include "contract/capability.hpp"

namespace ghostlock::contract {

    class ChildTask : public CapabilityInterface {
    public:
        /* Fetch the current child task handle; never cached across calls. */
        [[nodiscard]] virtual CapabilityResult<std::uint64_t>
        current() const noexcept = 0;

        [[nodiscard]] CapabilityKind kind() const noexcept final {
            return CapabilityKind::ChildTask;
        }

    protected:
        ~ChildTask() override = default; /* non-owning use: never deleted via base */
    };

} // namespace ghostlock::contract

#endif
