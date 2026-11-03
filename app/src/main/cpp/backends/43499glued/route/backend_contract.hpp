#ifndef GHOSTLOCK_BACKEND_CONTRACT_HPP
#define GHOSTLOCK_BACKEND_CONTRACT_HPP

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

#include "profile/model.h"
#include "route/backend_policy.hpp"
#include "route/component_catalog.hpp"
#include "session/stage_types.hpp"

namespace ghostlock::session {
    struct ExploitSession;
}

namespace ghostlock::runtime {
    /* Batch 5 backend contract. Two levels, one availability authority:
     *
     *   - BackendIdentity: the declared id used for selection/validation
     *     (route/backend_policy.hpp). Availability is owned by
     *     component_catalog::backend_available(); identity types never carry an
     *     availability state, so there is no second fact source.
     *   - BackendExecution<B, Middleware>: the steps an *available* backend
     *     provides for one middleware. Unavailable backends stop at
     *     BackendIdentity and are never instantiated through Pipeline. */
    template <class B>
    concept BackendIdentity = requires {
        { B::kind } -> std::convertible_to<BackendKind>;
    };

    template <class B, class Middleware>
    concept BackendExecution = BackendIdentity<B> &&
        requires(session::ExploitSession &exploit_session,
                 const profile::kernel_offsets &decoded, const char *debug_dir,
                 bool force_attack, session::VictimChain &chain) {
            { B::template run<Middleware>(exploit_session, decoded, debug_dir,
                                          force_attack, chain) }
                -> std::same_as<session::StageResult>;
        };

    /* The declared registry. Appending a backend lists it here once; the host
     * test walks it and checks every entry against the catalogue. */
    using BackendIdentityList = std::tuple<backend::Cve2026_43499, backend::Cve2026_64560>;

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
} // namespace ghostlock::runtime

#endif
