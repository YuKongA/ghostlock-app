#ifndef GHOSTLOCK_PLUGIN_HOST_OPS_HPP
#define GHOSTLOCK_PLUGIN_HOST_OPS_HPP

/* delta-4: adapt the per-run contract::Capabilities view into the C
 * glk_contract_ops function table an out-of-tree plugin receives.
 *
 * Authority: docs/analysis/contract-design.md sections 3.8, 3.10, 3.12.2, 4.3
 * and 5, and contract/abi/glk_contract_abi.h. The C ABI is the plugin-facing
 * projection of the same capabilities the tree-internal steps consume through
 * the C++ virtual interfaces; this header is the single adapter that turns one
 * into the other. A new capability is added to the C++ interface first and then
 * adapted here, in the same change.
 *
 * Mapping (kind -> operation):
 *   KernelRead / KernelWrite  read_u64/write_u64/read_bytes/write_bytes
 *   KernelWrite (Tier 1)      zero_word
 *   KernelAlias               image_to_direct_map
 *   ChildTask                 the glk_contract_ops.child_task value field
 *   (no surface yet)          query_u64/query_str return -EOPNOTSUPP
 *
 * Error contract (R7, no fake success). Every int32-returning operation maps the
 * contract::CapabilityError to a stable negative errno through
 * host_ops_error_code(); a capability the caller did not provide is
 * -EOPNOTSUPP, never a silent zero:
 *
 *   CapabilityError::None         0
 *   CapabilityError::Unsupported  -EOPNOTSUPP   (capability absent / NotSupported)
 *   CapabilityError::Unavailable  -EAGAIN       (declared but not ready)
 *   CapabilityError::Faulted      -EIO          (provided but execution failed)
 *   CapabilityError::Rejected     -EINVAL       (bad argument, fail-closed)
 *   CapabilityError::Closed       -EBADF        (used after close, terminal)
 *
 * The caller owns the HostOpsContext (non-owning, section 4.3); glk_contract_ops
 * only borrows a pointer to it. HostOpsContext::closed is the window terminal
 * flag: once set, every int32 op returns -EBADF before touching a capability.
 * image_to_direct_map returns uint64_t in the ABI and reports "cannot translate"
 * as 0 (the ABI's documented sentinel), including when the context is closed.
 *
 * R1: plugin -> {contract, memory, support}; this header includes only the
 * standard library and contract headers. */

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/abi/glk_contract_abi.h"
#include "contract/capabilities.hpp"
#include "contract/capability.hpp"
#include "contract/kernel_memory.hpp"

namespace ghostlock::plugin {

    /* The ABI error table, written as named constants so a test can assert every
     * entry without re-deriving a number. */
    inline constexpr std::int32_t kHostOpsOk = 0;
    inline constexpr std::int32_t kHostOpsUnsupported = -EOPNOTSUPP;
    inline constexpr std::int32_t kHostOpsUnavailable = -EAGAIN;
    inline constexpr std::int32_t kHostOpsFaulted = -EIO;
    inline constexpr std::int32_t kHostOpsRejected = -EINVAL;
    inline constexpr std::int32_t kHostOpsClosed = -EBADF;

    /* Single source of truth for the mapping; the operation thunks below all
     * route a CapabilityError through it. */
    [[nodiscard]] constexpr std::int32_t host_ops_error_code(
            contract::CapabilityError error) noexcept {
        switch (error) {
        case contract::CapabilityError::None: return kHostOpsOk;
        case contract::CapabilityError::Unsupported: return kHostOpsUnsupported;
        case contract::CapabilityError::Unavailable: return kHostOpsUnavailable;
        case contract::CapabilityError::Faulted: return kHostOpsFaulted;
        case contract::CapabilityError::Rejected: return kHostOpsRejected;
        case contract::CapabilityError::Closed: return kHostOpsClosed;
        }
        return kHostOpsFaulted;
    }

    /* Caller-owned, non-owning adaptation state. The window (or any other
     * caller) keeps this object alive for at least as long as the hooks that
     * receive the glk_contract_ops pointer. */
    struct HostOpsContext final {
        /* Non-owning; null means "no capability view". */
        const contract::Capabilities *capabilities = nullptr;
        /* The mechanism the adapter names when it calls the KernelMemory
         * primitives. The LKM window always uses LkmProxy. */
        contract::MemoryChannel channel = contract::MemoryChannel::LkmProxy;
        /* Terminal flag: set by close_host_ops() and initially true, so a
         * context that was never opened is closed rather than half-bound. */
        bool closed = true;
    };

    /* ---- per-module log routing (S4 logging batch B) --------------------- */

    /* The ABI's log() has no module id parameter, so attribution, budgeting and
     * rate limiting need a host-owned router: the host hands each module a COPY
     * of the caller's table whose log() is the router's thunk and whose ctx is
     * the router; every other entry forwards to the caller's ctx/ops unchanged.
     * A module therefore can neither spoof another id nor exceed its budget. */
    inline constexpr std::uint32_t kModuleLogBudgetPerRun = 64u;
    inline constexpr std::uint64_t kModuleLogMinIntervalNs = 1000000ull; /* 1 ms */
    inline constexpr std::size_t kModuleLogMaxBytes = 256u;

    struct ModuleLogRouter final {
        const glk_contract_ops *upstream = nullptr;
        const char *module_id = nullptr;
        std::uint32_t budget = kModuleLogBudgetPerRun;
        std::uint32_t emitted = 0u;
        std::uint32_t dropped = 0u;
        std::uint64_t last_ns = 0u;
    };

    /* Budget + rate decision. now_ns is CLOCK_MONOTONIC nanoseconds (0 = the
     * first message). Pure so the tests can drive the limiter without sleeping. */
    [[nodiscard]] bool module_log_accept(ModuleLogRouter &router,
                                         std::uint64_t now_ns) noexcept;

    /* Renders one accepted message into out (NUL-terminated, no newline):
     *   "<id> log(<level>): <msg>[ level_clamped=1][ ...]"
     * Control bytes and newlines in msg become '_' so a hostile message cannot
     * forge a second line; an over-long message is cut at kModuleLogMaxBytes and
     * gets a trailing "...". Pure: the thunk and the tests share it. */
    [[nodiscard]] std::string_view format_module_log(const ModuleLogRouter &router,
                                                     std::int32_t level,
                                                     std::string_view message, char *out,
                                                     std::size_t capacity) noexcept;

    /* Builds the table handed to one module's hooks (upstream may be null, which
     * yields a log-only table). router must outlive the returned table. */
    [[nodiscard]] glk_contract_ops make_module_ops(const glk_contract_ops *upstream,
                                                   ModuleLogRouter &router) noexcept;

    /* Fills ops with the ABI identity (size/abi_version/ctx) and every thunk,
     * and snapshots glk_contract_ops::child_task from ChildTask::current() when
     * that capability is present (0, the ABI's "not available" marker,
     * otherwise). HostOpsContext must outlive the ops pointer. */
    void init_host_ops(glk_contract_ops &ops, HostOpsContext &ctx) noexcept;

    /* Moves the context to the terminal Closed state. Every int32 op then
     * returns -EBADF and image_to_direct_map returns 0. */
    void close_host_ops(HostOpsContext &ctx) noexcept;

} // namespace ghostlock::plugin

#endif
