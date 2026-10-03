#ifndef GHOSTLOCK_TERMINAL_CONTRACT_HPP
#define GHOSTLOCK_TERMINAL_CONTRACT_HPP

#include <concepts>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

#include "pipeline/component_catalog.hpp"
#include "session/stage_types.hpp"
#include "terminal/terminal_input.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::pipeline::terminal {
    /* Batch 4 DECLARATION-ONLY scaffolding. These structs name the known
     * terminal ids and their failure reason; they are NOT wired into selection
     * and expose no provider operation or execution path. Availability is owned
     * by component_catalog::terminal_available(), and the static_asserts below
     * fail to compile if this declaration ever drifts from it.
     *
     * The root_child terminal is realized by terminal/root_child.*
     * (handoff step) with the victim/handoff code below.
     *
     * Responsibility split (do not merge):
     *   - child lifecycle boundary: session/victim_context.* + victim_process.*
     *     (pipe ends, child pid, retire/release);
     *   - root handoff / KernelSU manager verification: session/handoff_probe.*
     *     (module visibility, ksu log, enforce poll).
     * A future UMH terminal must not be bound to KernelSU. */

    struct RootChildTerminal final {
        static constexpr TerminalKind kind = TerminalKind::RootChild;
        static constexpr std::string_view unavailable_reason = "";
    };

    struct UmhForwardTerminal final {
        static constexpr TerminalKind kind = TerminalKind::UmhForward;
        static constexpr std::string_view unavailable_reason =
            "umh_forward terminal is not implemented";
    };

    /* Declarations must match the catalog authority. */
    static_assert(terminal_available(RootChildTerminal::kind));
    static_assert(!terminal_available(UmhForwardTerminal::kind));
} // namespace ghostlock::pipeline::terminal

namespace ghostlock::pipeline {
    /* Terminal contract, symmetric with BackendIdentity / BackendExecution
     * (pipeline/backend_contract.hpp): the declared identity, and the terminal
     * step an *available* terminal provides (startup/handoff). Availability is
     * owned by component_catalog::terminal_available(); neither level carries
     * an available state. */
    template <class F>
    concept TerminalIdentity = requires {
        { F::kind } -> std::convertible_to<TerminalKind>;
    };

    /* Unified terminal interface (ADR-0004 R19): an available terminal declares
     * its Input (derived from TerminalInput), its ActivationContext, and the step
     * that launches the App-selected RootProgram. Identity without a step is a
     * declaration-only placeholder (e.g. umh_forward before its execution lands). */
    template <class F>
    concept TerminalExecution = TerminalIdentity<F> &&
        requires {
            typename F::Input;
            requires std::derived_from<typename F::Input, ghostlock::terminal::TerminalInput>;
            { F::activation } -> std::convertible_to<ghostlock::terminal::ActivationContext>;
        } &&
        requires(session::CoreSession &exploit_session, typename F::Input &input) {
            { F::run(exploit_session, input) } -> std::same_as<session::StageResult>;
        };

    /* The declared registry; for_each keeps enumeration automatic as it grows. */
    using TerminalIdentityList =
        std::tuple<terminal::RootChildTerminal, terminal::UmhForwardTerminal>;

    template <class Fn, class... Fs>
    constexpr void for_each_terminal(Fn &&fn, std::tuple<Fs...> *) {
        (fn.template operator()<Fs>(), ...);
    }

    template <class Fn>
    constexpr void for_each_terminal(Fn &&fn) {
        for_each_terminal(std::forward<Fn>(fn),
                          static_cast<TerminalIdentityList *>(nullptr));
    }

    static_assert(TerminalIdentity<terminal::RootChildTerminal>);
    static_assert(TerminalIdentity<terminal::UmhForwardTerminal>);
} // namespace ghostlock::pipeline

#endif
