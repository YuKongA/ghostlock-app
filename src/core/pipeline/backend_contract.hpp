#ifndef GHOSTLOCK_BACKEND_CONTRACT_HPP
#define GHOSTLOCK_BACKEND_CONTRACT_HPP

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

#include "profile/model.h"
#include "pipeline/backend_policy.hpp"
#include "pipeline/component_catalog.hpp"
#include "session/stage_types.hpp"
#include "terminal/terminal_input.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::pipeline {
    /* Batch 5 backend contract, generalized to the terminal input type (R10
     * continuation). Two levels, one availability authority:
     *
     *   - BackendIdentity: the declared id used for selection/validation
     *     (pipeline/backend_policy.hpp). Availability is owned by
     *     component_catalog::backend_available(); identity types never carry an
     *     availability state, so there is no second fact source.
     *   - BackendExecution<B, Input>: the steps an *available* backend provides
     *     for one terminal input type. Input is the paired terminal's
     *     Terminal::Input (RootedChild for root_child, UmhForwardInput for
     *     umh_forward), so a backend only satisfies the concept for the inputs
     *     it can actually fill. Unavailable backends stop at BackendIdentity and
     *     are never instantiated through Pipeline. */
    template <class B>
    concept BackendIdentity = requires {
        { B::kind } -> std::convertible_to<BackendKind>;
    };

    template <class B, class Input>
    concept BackendExecution = BackendIdentity<B> &&
        std::derived_from<Input, ghostlock::terminal::TerminalInput> &&
        requires(session::CoreSession &exploit_session,
                 const profile::kernel_offsets &decoded, const char *debug_dir,
                 bool force_attack, Input &input) {
            { B::run(exploit_session, decoded, debug_dir, force_attack, input) }
                -> std::same_as<session::StageResult>;
        };

    /* Optional per-backend state contract (ADR-0002 / D3). A backend whose
     * execution needs the opaque CoreSession slot provides its concrete State
     * plus noexcept construct/destroy. Pipeline binds both to the run scope
     * (RAII), so every failure/early-return path tears the state down. The
     * declaration-only placeholders carry no state and omit all three;
     * BackendExecution is the gate that keeps them out of Pipeline. */
    template <class B>
    concept BackendState = requires(session::CoreSession &exploit_session) {
        typename B::State;
        { B::state_construct(exploit_session) } noexcept;
        { B::state_destroy(exploit_session) } noexcept;
    };

    /* The declared registry. Appending a backend lists it here once; the host
     * test walks it and checks every entry against the catalogue. */
    using BackendIdentityList =
        std::tuple<backend::Cve2026_43499, backend::Cve2026_64560, backend::Cve2026_31431,
                   backend::Cve2026_43503, backend::Cve2026_23274, backend::Cve2026_43284>;

    template <class Fn, class... Bs>
    constexpr void for_each_backend(Fn &&fn, std::tuple<Bs...> *) {
        (fn.template operator()<Bs>(), ...);
    }

    template <class Fn>
    constexpr void for_each_backend(Fn &&fn) {
        for_each_backend(std::forward<Fn>(fn),
                         static_cast<BackendIdentityList *>(nullptr));
    }

    static_assert(BackendIdentity<backend::Cve2026_43499>);
    static_assert(BackendIdentity<backend::Cve2026_64560>);
    static_assert(BackendIdentity<backend::Cve2026_31431>);
    static_assert(BackendIdentity<backend::Cve2026_43503>);
    static_assert(BackendIdentity<backend::Cve2026_23274>);
    static_assert(BackendIdentity<backend::Cve2026_43284>);
} // namespace ghostlock::pipeline

#endif
