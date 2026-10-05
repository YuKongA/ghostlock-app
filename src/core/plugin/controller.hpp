#ifndef GHOSTLOCK_PLUGIN_CONTROLLER_HPP
#define GHOSTLOCK_PLUGIN_CONTROLLER_HPP

#include <cstdint>
#include <tuple>
#include <utility>

#include "plugin/policy.hpp"
#include "plugin/registry.hpp"

namespace ghostlock::plugin {
    namespace detail {
        template <class Fn, class... Ps>
        constexpr void for_each_policy(Fn &&fn, std::tuple<Ps...> *) {
            (fn.template operator()<Ps>(), ...);
        }
    } // namespace detail

    /* Invoke fn<P>() for every policy in the caller-supplied list. The list is a
     * std::tuple of policy types; the mechanism itself fixes no behaviors, and
     * the caller decides which of them are enabled through the controller's
     * injected gate. */
    template <class PolicyList, class Fn>
    constexpr void for_each_plugin_policy(Fn &&fn) {
        detail::for_each_policy(std::forward<Fn>(fn),
                                static_cast<PolicyList *>(nullptr));
    }

    /* Maps the mechanism's three stage boundaries onto the ABI vocabulary. The
     * in-tree and external channels share PluginStage; the ABI names the last
     * boundary PRE_TERMINAL. */
    [[nodiscard]] constexpr contract::CountermeasureStage to_countermeasure_stage(
            PluginStage stage) noexcept {
        switch (stage) {
        case PluginStage::PreSpawn:
            return contract::CountermeasureStage::PreSpawn;
        case PluginStage::PostSpawn:
            return contract::CountermeasureStage::PostSpawn;
        case PluginStage::PreHandoff:
            return contract::CountermeasureStage::PreTerminal;
        }
        return contract::CountermeasureStage::PreSpawn;
    }

    /* The controller. The registry, the gate and the context are injected by the
     * caller:
     *   - PolicyList (a template argument) is the tuple of behaviors to walk;
     *   - gate is a functor with template <class P> bool operator()() that says
     *     whether P applies here (the backend reads P::enabled(view) from its
     *     own state), so this header knows no profile, no backend and no vendor
     *     behavior;
     *   - capabilities is the caller-owned non-owning `contract::Capabilities`
     *     aggregate; the route's compile-time-bound write primitive reaches the
     *     behaviors through its KernelMemory adapter, so the controller carries
     *     no middleware template parameter;
     *   - context is a caller-owned value of the behaviors' view type, threaded
     *     to every policy unchanged (the neutral mechanism never inspects it).
     * Header-only and host-compilable.
     *
     * CM-3 adds a second, caller-driven channel: an out-of-tree hook table. The
     * five-argument apply is the in-tree channel and its body/order are
     * unchanged. The overload with a RuntimeRegistry runs the in-tree policies
     * first (same order) and then the registry hooks of the same stage, in
     * (priority, registration) order, gated by the caller. A non-zero hook
     * return records a failure and disables the module's later stages; dispatch
     * continues (the stage, not this mechanism, decides whether to abort).
     * The default call sites pass no registry, so a run with no module remains
     * byte-for-byte the run of today. */
    template <class PolicyList>
    struct PluginController final {
        template <class Gate, class Context>
        static Status apply(PluginStage stage, CoreSession &session,
                            const contract::Capabilities &capabilities, Gate &&gate,
                            const Context &context) {
            Status ok = true;
            for_each_plugin_policy<PolicyList>([&]<class P>() {
                if (gate.template operator()<P>()) {
                    ok = P::apply(stage, session, capabilities, context) && ok;
                }
            });
            return ok;
        }

        template <class Gate, class Context, class ExternalGate>
        static Status apply(PluginStage stage, CoreSession &session,
                            const contract::Capabilities &capabilities, Gate &&gate,
                            const Context &context, RuntimeRegistry &registry,
                            const glk_contract_ops *host, ExternalGate &&external_gate) {
            Status ok = apply(stage, session, capabilities,
                              std::forward<Gate>(gate), context);
            const contract::CountermeasureStage abi_stage =
                    to_countermeasure_stage(stage);
            registry.for_each(abi_stage, [&](const RegistryHook &hook) {
                if (!external_gate(hook)) {
                    return;
                }
                const std::int32_t rc = hook.fn(
                        hook.user,
                        static_cast<glk_stage>(
                                static_cast<std::uint32_t>(hook.stage)),
                        host);
                if (rc != 0) {
                    ok = false;
                    registry.record_stage_failure(hook, rc);
                    registry.mark_module_failed(hook.module_slot);
                }
            });
            return ok;
        }
    };
} // namespace ghostlock::plugin

#endif
