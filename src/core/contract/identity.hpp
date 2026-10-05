#ifndef GHOSTLOCK_CONTRACT_IDENTITY_HPP
#define GHOSTLOCK_CONTRACT_IDENTITY_HPP

/* ADR-0004 identity vocabulary and interface contracts.
 *
 * This is the contract layer: the stable component ids, the only availability
 * authority, the neutral terminal input interface and the compile-time concepts
 * a backend/terminal satisfies. It must stay host-compilable and must not
 * include backend/, pipeline/, platform/ or terminal/ (the firewall enforces
 * that edge). The sparse (backend, steps, terminal) dispatch catalogue lives in
 * pipeline/component_catalog.hpp, which composes this header.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <string_view>
#include <tuple>
#include <utility>

#include "contract/stage_result.hpp"
#include "profile/schema.hpp"

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::contract {
    /* Stable component ids. Explicit numeric values; never rely on the
     * compiler's enum layout. The UMH terminal and the cve_2026_64560/31431/
     * 43503/23274/43284 backends are reserved and report unavailable until
     * their own batches land. */
    enum class TerminalKind : std::uint8_t {
        RootChild = 1,
        UmhForward = 2,
    };

    enum class BackendKind : std::uint8_t {
        Cve2026_43499 = 1,
        Cve2026_64560 = 2,
        Cve2026_31431 = 3,
        Cve2026_43503 = 4,
        Cve2026_23274 = 5,
        Cve2026_43284 = 6,
    };

    /* StepSet is backend-owned (ADR-0004 R18) but App-visible: the profile names
     * which step sequence the backend runs. W1W2 skips the seccomp bypass (shell
     * entry or kernel-spawned launch); W1W3 includes it (app-descendant launch).
     * PageCacheWrite is the cve_2026_43284 step vocabulary. */
    enum class StepSetKind : std::uint8_t {
        /* No catalogued step set: the wire carried an absent/unknown id. It is
         * never available, so the selection gate rejects it (historical
         * fail-closed behaviour, now expressed without an out-of-range cast). */
        Unknown = 0,
        W1W2 = 1,
        W1W3 = 2,
        PageCacheWrite = 3,
    };

    struct ComponentSelection final {
        BackendKind backend;
        StepSetKind steps;
        TerminalKind terminal;
    };

    /* AVAILABILITY FLIP (B6/T5). The production seam for
     * {Cve2026_43284, PageCacheWrite, UmhForward} is now wired end to end
     * (main.cpp composition-root binding -> Pipeline -> backend_terminal), but
     * this batch deliberately keeps availability false: it flips only after the
     * main agent re-runs app-call on a real device and sees the full chain PASS.
     * The flip is exactly these two lines plus three static_asserts:
     *   terminal_available:  ... || kind == TerminalKind::UmhForward
     *   backend_available:   ... || kind == BackendKind::Cve2026_43284
     * then update the two static_asserts in this file (backend namespace:
     * !backend_available(Cve2026_43284::kind); terminal namespace:
     * !terminal_available(UmhForwardTerminal::kind)) and the one in
     * backend/cve_2026_43284_backend.hpp
     * (!contract::backend_available(Cve2026_43284Policy::kind)).
     * No other change is needed: the catalogue, pipeline, dispatch, wire and
     * Kotlin routing are already in place. */
    [[nodiscard]] constexpr bool terminal_available(TerminalKind kind) noexcept {
        return kind == TerminalKind::RootChild ||
               kind == TerminalKind::UmhForward;
    }

    [[nodiscard]] constexpr bool backend_available(BackendKind kind) noexcept {
        return kind == BackendKind::Cve2026_43499 ||
               kind == BackendKind::Cve2026_43284;
    }

    [[nodiscard]] constexpr bool stepset_available(StepSetKind kind) noexcept {
        return kind == StepSetKind::W1W2 || kind == StepSetKind::W1W3 ||
               kind == StepSetKind::PageCacheWrite;
    }

    /* Per-axis availability pre-check: the runtime fail-closed gate, and the
     * only fact that says a selection may actually run on this build/device.
     * backend_available(Cve2026_43284) stays false until the B6/T5 app-call
     * device gate even though the 43284 triple is catalogued and its production
     * seam is wired; the composition root checks this before dispatch, so
     * catalogued-but-unverified never runs. */
    [[nodiscard]] constexpr bool selection_supported(
        const ComponentSelection &selection) noexcept {
        return backend_available(selection.backend) &&
               stepset_available(selection.steps) &&
               terminal_available(selection.terminal);
    }

    /* Neutral terminal interface vocabulary (ADR-0004 R10/R19/R20). It is kept
     * in contract, not terminal/, so the execution concepts below can name it
     * without creating a contract -> terminal include edge. The concrete
     * terminal inputs (RootedChild, UmhForwardInput) and terminal policies stay
     * in terminal/. */

    /* Which root program the App selected for this session (single value). The
     * program is a parameter, never a compile-time binding; both the root_child
     * and the umh_forward terminal can launch it. */
    enum class RootProgramKind : std::uint8_t {
        KernelSU = 0,
        FolkPatch = 1,
        Custom = 2,
    };

    /* Neutral, host-safe spec: kind plus a bounded argv string (program path and
     * arguments as the launcher receives them). No heap, trivially copyable. */
    struct RootProgram final {
        static constexpr std::size_t kArgvCapacity = 192;

        RootProgramKind kind = RootProgramKind::KernelSU;
        std::array<char, kArgvCapacity> argv{};

        /* Bounded copy; always NUL-terminates, truncates rather than overflows. */
        void set_argv(std::string_view text) noexcept {
            const std::size_t n = text.size() < kArgvCapacity - 1 ? text.size() : kArgvCapacity - 1;
            for (std::size_t i = 0; i < n; ++i) argv[i] = text[i];
            argv[n] = '\0';
        }

        [[nodiscard]] std::string_view argv_view() const noexcept {
            return std::string_view(argv.data());
        }
    };

    /* How a terminal launches the root program (ADR-0004 R19/R20). Descendant
     * inherits the entry process's seccomp filter; KernelSpawned (UMH) does not. */
    enum class ActivationContext : std::uint8_t { Descendant, KernelSpawned };

    /* Neutral terminal input (ADR-0004 R10/D2): what a backend hands to the
     * terminal, independent of the concrete terminal. Every terminal input
     * carries the App-selected root program. RootedChild and UmhForwardInput
     * derive from it; the pipeline passes the base reference. */
    struct TerminalInput {
        RootProgram root_program{};
    };

    /* ---- Declared backend identities (ADR-0004 batch 4/5) ----
     *
     * These structs name the known backend ids; they expose no execution path
     * and no availability logic. Availability is owned by backend_available(),
     * and the static_asserts below fail to compile if this declaration drifts
     * from it. The cve_2026_64560/31431/43503/23274/43284 ids are known but
     * unavailable (rejected before the attack by the orchestrator). */
    namespace backend {
        struct Cve2026_43499 final {
            static constexpr BackendKind kind = BackendKind::Cve2026_43499;
        };

        struct Cve2026_64560 final {
            static constexpr BackendKind kind = BackendKind::Cve2026_64560;
        };

        struct Cve2026_31431 final {
            static constexpr BackendKind kind = BackendKind::Cve2026_31431;
        };

        struct Cve2026_43503 final {
            static constexpr BackendKind kind = BackendKind::Cve2026_43503;
        };

        struct Cve2026_23274 final {
            static constexpr BackendKind kind = BackendKind::Cve2026_23274;
        };

        struct Cve2026_43284 final {
            static constexpr BackendKind kind = BackendKind::Cve2026_43284;
        };

        static_assert(backend_available(Cve2026_43499::kind));
        static_assert(!backend_available(Cve2026_64560::kind));
        static_assert(!backend_available(Cve2026_31431::kind));
        static_assert(!backend_available(Cve2026_43503::kind));
        static_assert(!backend_available(Cve2026_23274::kind));
        static_assert(backend_available(Cve2026_43284::kind));
    } // namespace backend

    /* ---- Declared terminal identities (ADR-0004 batch 4) ----
     *
     * Declaration-only scaffolding. These structs name the known terminal ids
     * and their failure reason; they are NOT wired into selection and expose no
     * provider operation or execution path. Availability is owned by
     * terminal_available(), and the static_asserts below fail to compile if this
     * declaration ever drifts from it.
     *
     * The root_child terminal is realized by terminal/root_child.*; the
     * umh_forward terminal by terminal/umh_forward.*. Responsibility split (do
     * not merge):
     *   - child lifecycle boundary: session/victim_context.* + victim_process.*;
     *   - root handoff / KernelSU manager verification: session/handoff_probe.*;
     *   - UMH forward/wait: terminal/umh_forward.* (must not be bound to
     *     KernelSU).
     */
    namespace terminal {
        struct RootChildTerminal final {
            static constexpr TerminalKind kind = TerminalKind::RootChild;
            static constexpr std::string_view unavailable_reason = "";
        };

        struct UmhForwardTerminal final {
            static constexpr TerminalKind kind = TerminalKind::UmhForward;
            /* B5-8 + B6/T5: the read-only readiness policy and its production
             * probe are wired and passed the app-call device gate (2026-10-05). */
            static constexpr std::string_view unavailable_reason =
                "umh_forward terminal is not device-verified (app-call gate)";
        };

        /* Declarations must match the catalog authority. */
        static_assert(terminal_available(RootChildTerminal::kind));
        static_assert(terminal_available(UmhForwardTerminal::kind));
    } // namespace terminal

    /* ---- Execution contracts (ADR-0004 R10/R18/R19) ----
     *
     * Two levels per axis, one availability authority:
     *   - BackendIdentity: the declared id used for selection/validation.
     *     Availability is owned by backend_available(); identity types never
     *     carry an availability state, so there is no second fact source.
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
        std::derived_from<Input, TerminalInput> &&
        requires(session::CoreSession &exploit_session,
                 const profile::Document &document, const char *debug_dir,
                 bool force_attack, Input &input) {
            /* The backend owns both the profile bind (state_from) and the run.
             * state_from validates the neutral Document against the backend's
             * own owner Schema and installs the backend state; the composition
             * layer never names the frozen transport (ADR-0004 F1/A2-5). */
            { B::state_from(exploit_session, document) }
                -> std::same_as<profile::BindStatus>;
            { B::run(exploit_session, debug_dir, force_attack, input) }
                -> std::same_as<StageResult>;
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

    /* Declared registry. The host test walks it and checks every entry against
     * the catalogue. */
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

    /* Terminal contract, symmetric with BackendIdentity / BackendExecution: the
     * declared identity, and the terminal step an *available* terminal provides
     * (startup/handoff). Availability is owned by terminal_available(); neither
     * level carries an available state. */
    template <class F>
    concept TerminalIdentity = requires {
        { F::kind } -> std::convertible_to<TerminalKind>;
    };

    /* Unified terminal interface (ADR-0004 R19): every terminal declares its
     * Input (derived from TerminalInput), its ActivationContext, and the step
     * that launches the App-selected RootProgram. Both current terminals provide
     * the step; availability is still owned separately by the catalogue, so a
     * terminal can satisfy this contract while remaining unavailable. */
    template <class F>
    concept TerminalExecution = TerminalIdentity<F> &&
        requires {
            typename F::Input;
            requires std::derived_from<typename F::Input, TerminalInput>;
            { F::activation } -> std::convertible_to<ActivationContext>;
        } &&
        requires(session::CoreSession &exploit_session, typename F::Input &input) {
            { F::run(exploit_session, input) } -> std::same_as<StageResult>;
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
} // namespace ghostlock::contract

#endif
