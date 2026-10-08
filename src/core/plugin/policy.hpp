#ifndef GHOSTLOCK_PLUGIN_POLICY_HPP
#define GHOSTLOCK_PLUGIN_POLICY_HPP

#include <concepts>
#include <cstdint>

#include "contract/capabilities.hpp"
#include "support/status.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::plugin {
    using ghostlock::session::CoreSession;

    /* When the controller runs a behavior, relative to the backend attack steps:
     *   PreSpawn   - W1 done, before the victim is spawned;
     *   PostSpawn  - the rooted child exists (today's W2b);
     *   PreHandoff - before the terminal handoff. */
    enum class PluginStage : std::uint8_t {
        PreSpawn = 0,
        PostSpawn = 1,
        PreHandoff = 2,
    };

    /* Behaviors consume the contract capability aggregate directly (ADR-0004
     * R4; contract-design.md sections 3.9, 5 and 11). The retired interim
     * `AncillaryOps` bundle carried a bool + two raw function pointers + the
     * raw child task; the neutral mechanism now takes a non-owning
     * `const contract::Capabilities &` so every call goes through the typed
     * interface (KernelMemory / KernelAlias / ChildTask) and an unsupported
     * capability is an explicit error instead of a zero or a null pointer.
     *
     * Lifetime: the caller owns the Capabilities and its adapters. They are
     * call-block-local at the backend call site, so a behavior must finish its
     * work before that block exits; the mechanism never stores the reference. */

    /* Neutral defaults so a behavior can opt into the no-op path. The gate is
     * the caller's now (the controller consults an injected functor), so the
     * mechanism carries no `enabled(view)` here. */
    struct PluginPolicyDefaults {
        template <class Context>
        static Status apply(PluginStage, CoreSession &,
                            const contract::Capabilities &, const Context &) noexcept {
            return true;
        }
    };

    /* Ancillary-behavior contract: one stage entry that reads the caller's
     * capability aggregate and the caller-supplied Context (the neutral view the
     * behavior reads). The contract deliberately says nothing about how a
     * behavior is gated: the caller injects both the registry (a tuple) and the
     * gate. */
    template <class P, class Context>
    concept PluginPolicyFor =
        requires(CoreSession &session, const contract::Capabilities &capabilities,
                 const Context &context) {
            { P::apply(PluginStage::PreSpawn, session, capabilities,
                       context) } -> std::same_as<Status>;
        };
} // namespace ghostlock::plugin

#endif
