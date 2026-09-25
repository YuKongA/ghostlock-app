#ifndef GHOSTLOCK_FRONTEND_CONTRACT_HPP
#define GHOSTLOCK_FRONTEND_CONTRACT_HPP

#include <concepts>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

#include "route/component_catalog.hpp"
#include "session/stage_types.hpp"

namespace ghostlock::session {
    struct ExploitSession;
}

namespace ghostlock::runtime::frontend {
    /* Batch 4 DECLARATION-ONLY scaffolding. These structs name the known
     * frontend ids and their failure reason; they are NOT wired into selection
     * and expose no provider operation or execution path. Availability is owned
     * by component_catalog::frontend_available(), and the static_asserts below
     * fail to compile if this declaration ever drifts from it.
     *
     * The root_child frontend is realized by session/root_child_frontend.*
     * (handoff step) with the victim/handoff code below.
     *
     * Responsibility split (do not merge):
     *   - child lifecycle boundary: session/victim_context.* + victim_process.*
     *     (pipe ends, child pid, retire/release);
     *   - root handoff / KernelSU manager verification: session/handoff_probe.*
     *     (module visibility, ksu log, enforce poll).
     * A future UMH frontend must not be bound to KernelSU. */

    struct RootChildFrontend final {
        static constexpr FrontendKind kind = FrontendKind::RootChild;
        static constexpr std::string_view unavailable_reason = "";
    };

    struct UmhForwardFrontend final {
        static constexpr FrontendKind kind = FrontendKind::UmhForward;
        static constexpr std::string_view unavailable_reason =
            "umh_forward frontend is not implemented";
    };

    /* Declarations must match the catalog authority. */
    static_assert(frontend_available(RootChildFrontend::kind));
    static_assert(!frontend_available(UmhForwardFrontend::kind));
} // namespace ghostlock::runtime::frontend

namespace ghostlock::runtime {
    /* Frontend contract, symmetric with BackendIdentity / BackendExecution
     * (route/backend_contract.hpp): the declared identity, and the terminal
     * step an *available* frontend provides (startup/handoff). Availability is
     * owned by component_catalog::frontend_available(); neither level carries
     * an available state. */
    template <class F>
    concept FrontendIdentity = requires {
        { F::kind } -> std::convertible_to<FrontendKind>;
    };

    template <class F>
    concept FrontendExecution = FrontendIdentity<F> &&
        requires(session::ExploitSession &exploit_session,
                 const session::VictimChain &chain) {
            { F::run(exploit_session, chain) } -> std::same_as<session::StageResult>;
        };

    /* The declared registry; for_each keeps enumeration automatic as it grows. */
    using FrontendIdentityList =
        std::tuple<frontend::RootChildFrontend, frontend::UmhForwardFrontend>;

    template <class Fn, class... Fs>
    constexpr void for_each_frontend(Fn &&fn, std::tuple<Fs...> *) {
        (fn.template operator()<Fs>(), ...);
    }

    template <class Fn>
    constexpr void for_each_frontend(Fn &&fn) {
        for_each_frontend(std::forward<Fn>(fn),
                          static_cast<FrontendIdentityList *>(nullptr));
    }

    static_assert(FrontendIdentity<frontend::RootChildFrontend>);
    static_assert(FrontendIdentity<frontend::UmhForwardFrontend>);
} // namespace ghostlock::runtime

#endif
