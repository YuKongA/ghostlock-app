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

#include "contract/model.hpp"
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

    /* ---- S4 R6b combination tokens (ADR-0006 T5) ----
     *
     * The user-visible selection is ONE token living in backend.<id>.steps. A
     * token is "<route>_<path>" for a backend with a route axis (cve_2026_43499)
     * and a bare "<path>" for a backend without one (cve_2026_43284). The token
     * derives the internal (route, step set, terminal/path) triple; that derived
     * triple is an implementation detail, no longer independently selectable.
     *
     * This table is the vocabulary authority (contract); pipeline/catalog only
     * composes it. available=false is a *planned* item: it parses and is known,
     * but the selection gate rejects it (and the App greys it out).
     *
     * F3: a backend without a route axis carries RouteKind::None, never the
     * deprecated legacy Auto; the GLKv3 resolver rejects a document whose root
     * route disagrees with this column and rejects a token that needs a route
     * when the document declares none. */
    enum class CombinationKind : std::uint8_t {
        Unknown = 0,
        /* cve_2026_43499: route prefix required. */
        McastRootchild,
        PselectRootchild,
        TcpRootchild,
        McastShizuku,
        PselectShizuku,
        TcpShizuku,
        McastUmh,
        PselectUmh,
        TcpUmh,
        /* cve_2026_43284: no route axis, bare path. */
        Umh,
        Rootchild,
        Shizuku,
    };

    /* F1 (R6b v3 design patch 7.1): the flat product is decomposed into two
     * ORTHOGONAL vocabularies plus the owning backend. PathKind is the
     * user-visible handoff path; it is not derivable from the terminal (both
     * rootchild and shizuku enter the root child and differ only in the step
     * set), which is exactly why it needs its own axis.
     *
     * CombinationKind stays the compact id (its enum value is the uint8 wire
     * slot in profile::Document::combination), so this decomposition is a
     * compile-time view and NOT a wire or storage change. New backends extend
     * the catalogue by adding rows, not by adding enum values per route/path
     * product. */
    enum class PathKind : std::uint8_t {
        Rootchild = 1,
        Shizuku = 2,
        Umh = 3,
    };

    struct CombinationId final {
        BackendKind backend;
        profile::RouteKind route;
        PathKind path;
    };

    /* Field order is padding-optimal (two 16-byte views first, then the small
     * enum ids and the flag) so clang-analyzer's performance.Padding check stays
     * clean: 16 + 16 + 7 one-byte fields = 39 bytes, which the 8-byte alignment
     * rounds to 40 with no interior padding. */
    struct CombinationSpec final {
        std::string_view token;
        /* Dropdown summary the App shows, carried as catalogue DATA so the UI
         * text has one authority instead of a second formatting table. */
        std::string_view doc;
        BackendKind backend;
        CombinationKind kind;
        profile::RouteKind route;
        PathKind path;
        StepSetKind steps;
        TerminalKind terminal;
        bool available;
    };

    /* The whitelist. Only these 12 tokens are ever accepted; anything else is
     * rejected with the token text echoed by the composition root. */
    inline constexpr CombinationSpec kCombinationCatalog[] = {
        {"mcast_rootchild", "cve_2026_43499 \u00b7 multicast_waiter \u00b7 w1_w3 \u00b7 root_child",
         BackendKind::Cve2026_43499, CombinationKind::McastRootchild,
         profile::RouteKind::MulticastWaiter, PathKind::Rootchild, StepSetKind::W1W3, TerminalKind::RootChild, true},
        {"pselect_rootchild", "cve_2026_43499 \u00b7 select_stack \u00b7 w1_w3 \u00b7 root_child",
         BackendKind::Cve2026_43499, CombinationKind::PselectRootchild,
         profile::RouteKind::SelectStack, PathKind::Rootchild, StepSetKind::W1W3, TerminalKind::RootChild, true},
        {"tcp_rootchild", "cve_2026_43499 \u00b7 tcp_zerocopy \u00b7 w1_w3 \u00b7 root_child",
         BackendKind::Cve2026_43499, CombinationKind::TcpRootchild,
         profile::RouteKind::TcpZerocopy, PathKind::Rootchild, StepSetKind::W1W3, TerminalKind::RootChild, true},
        {"mcast_shizuku", "cve_2026_43499 \u00b7 multicast_waiter \u00b7 w1_w2 \u00b7 root_child",
         BackendKind::Cve2026_43499, CombinationKind::McastShizuku,
         profile::RouteKind::MulticastWaiter, PathKind::Shizuku, StepSetKind::W1W2, TerminalKind::RootChild, true},
        {"pselect_shizuku", "cve_2026_43499 \u00b7 select_stack \u00b7 w1_w2 \u00b7 root_child",
         BackendKind::Cve2026_43499, CombinationKind::PselectShizuku,
         profile::RouteKind::SelectStack, PathKind::Shizuku, StepSetKind::W1W2, TerminalKind::RootChild, true},
        {"tcp_shizuku", "cve_2026_43499 \u00b7 tcp_zerocopy \u00b7 w1_w2 \u00b7 root_child",
         BackendKind::Cve2026_43499, CombinationKind::TcpShizuku,
         profile::RouteKind::TcpZerocopy, PathKind::Shizuku, StepSetKind::W1W2, TerminalKind::RootChild, true},
        {"mcast_umh", "cve_2026_43499 \u00b7 multicast_waiter \u00b7 w1_w3 \u00b7 umh_forward",
         BackendKind::Cve2026_43499, CombinationKind::McastUmh,
         profile::RouteKind::MulticastWaiter, PathKind::Umh, StepSetKind::W1W3, TerminalKind::UmhForward, false},
        {"pselect_umh", "cve_2026_43499 \u00b7 select_stack \u00b7 w1_w3 \u00b7 umh_forward",
         BackendKind::Cve2026_43499, CombinationKind::PselectUmh,
         profile::RouteKind::SelectStack, PathKind::Umh, StepSetKind::W1W3, TerminalKind::UmhForward, false},
        {"tcp_umh", "cve_2026_43499 \u00b7 tcp_zerocopy \u00b7 w1_w3 \u00b7 umh_forward",
         BackendKind::Cve2026_43499, CombinationKind::TcpUmh,
         profile::RouteKind::TcpZerocopy, PathKind::Umh, StepSetKind::W1W3, TerminalKind::UmhForward, false},
        {"umh", "cve_2026_43284 \u00b7 pagecache_write \u00b7 umh_forward",
         BackendKind::Cve2026_43284, CombinationKind::Umh,
         profile::RouteKind::None, PathKind::Umh, StepSetKind::PageCacheWrite, TerminalKind::UmhForward, true},
        {"rootchild", "cve_2026_43284 \u00b7 pagecache_write \u00b7 root_child",
         BackendKind::Cve2026_43284, CombinationKind::Rootchild,
         profile::RouteKind::None, PathKind::Rootchild, StepSetKind::PageCacheWrite, TerminalKind::RootChild, false},
        {"shizuku", "cve_2026_43284 \u00b7 pagecache_write \u00b7 root_child",
         BackendKind::Cve2026_43284, CombinationKind::Shizuku,
         profile::RouteKind::None, PathKind::Shizuku, StepSetKind::PageCacheWrite, TerminalKind::RootChild, false},
    };

    [[nodiscard]] constexpr const CombinationSpec *combination_spec(
            CombinationKind kind) noexcept {
        for (const CombinationSpec &spec : kCombinationCatalog) {
            if (spec.kind == kind) return &spec;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr bool combination_resolve(
            BackendKind backend, std::string_view token,
            CombinationKind &out) noexcept {
        for (const CombinationSpec &spec : kCombinationCatalog) {
            if (spec.backend == backend && spec.token == token) {
                out = spec.kind;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::string_view combination_name(
            CombinationKind kind) noexcept {
        const CombinationSpec *spec = combination_spec(kind);
        return spec != nullptr ? spec->token : std::string_view{};
    }

    [[nodiscard]] constexpr bool combination_available(CombinationKind kind) noexcept {
        const CombinationSpec *spec = combination_spec(kind);
        return spec != nullptr && spec->available;
    }

    /* F1 decomposition accessors. The token table stays the single authority:
     * the id is a VIEW of a catalogue row (never an independent selection), and
     * the reverse lookup is total over the 12 rows because (backend, route,
     * path) is unique there (asserted by combination_manifest_test). */
    [[nodiscard]] constexpr bool combination_id(CombinationKind kind,
                                                CombinationId &out) noexcept {
        const CombinationSpec *spec = combination_spec(kind);
        if (spec == nullptr) return false;
        out = CombinationId{spec->backend, spec->route, spec->path};
        return true;
    }

    [[nodiscard]] constexpr CombinationKind combination_from_id(
            CombinationId id) noexcept {
        for (const CombinationSpec &spec : kCombinationCatalog) {
            if (spec.backend == id.backend && spec.route == id.route &&
                spec.path == id.path) {
                return spec.kind;
            }
        }
        return CombinationKind::Unknown;
    }

    /* Handoff-path token (App dropdown and the exported manifests). */
    [[nodiscard]] constexpr std::string_view path_name(PathKind kind) noexcept {
        switch (kind) {
            case PathKind::Rootchild: return "rootchild";
            case PathKind::Shizuku: return "shizuku";
            case PathKind::Umh: return "umh";
        }
        return "unknown";
    }

    /* Route/middleware token spelling. This is the ONE route name table:
     * pipeline::middleware_name forwards here, and the exported manifests use
     * it too. RouteKind::None, the deprecated legacy Auto value 0 (deliberately
     * not named) and any unknown id all report as "none". */
    [[nodiscard]] constexpr std::string_view route_name(profile::RouteKind kind) noexcept {
        switch (kind) {
            case profile::RouteKind::TcpZerocopy: return "tcp_zerocopy";
            case profile::RouteKind::SelectStack: return "select_stack";
            case profile::RouteKind::MulticastWaiter: return "multicast_waiter";
            default: return "none";
        }
    }

    /* Token -> internal StepSet id (the owner Schema's numeric View slot).
     *
     * S1 (M2 item 4): the CHECKED form. A token that names no catalogued
     * combination for this backend returns false so the owner bind fails closed
     * with its section and key; the previous unchecked helper returned 0, which
     * the bind stored silently (0 is also a legitimate-looking wire value). */
    [[nodiscard]] constexpr bool combination_stepset_wire_checked(
            BackendKind backend, std::string_view token,
            std::uint16_t &out) noexcept {
        CombinationKind kind = CombinationKind::Unknown;
        if (!combination_resolve(backend, token, kind)) return false;
        const CombinationSpec *spec = combination_spec(kind);
        if (spec == nullptr) return false;
        out = static_cast<std::uint16_t>(spec->steps);
        return true;
    }

    /* Stable backend token <-> id (owner section prefix / root selection). The
     * composition root and the profile framing bridge share this vocabulary so
     * it cannot drift from the catalogue. */
    [[nodiscard]] constexpr std::string_view backend_token_name(BackendKind kind) noexcept {
        switch (kind) {
            case BackendKind::Cve2026_43499: return "cve_2026_43499";
            case BackendKind::Cve2026_64560: return "cve_2026_64560";
            case BackendKind::Cve2026_31431: return "cve_2026_31431";
            case BackendKind::Cve2026_43503: return "cve_2026_43503";
            case BackendKind::Cve2026_23274: return "cve_2026_23274";
            case BackendKind::Cve2026_43284: return "cve_2026_43284";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr std::string_view terminal_token_name(
            TerminalKind kind) noexcept {
        return kind == TerminalKind::RootChild ? "root_child" : "umh_forward";
    }

    [[nodiscard]] constexpr bool terminal_kind_from_token(
            std::string_view token, TerminalKind &out) noexcept {
        for (const TerminalKind kind : {TerminalKind::RootChild, TerminalKind::UmhForward}) {
            if (token == terminal_token_name(kind)) {
                out = kind;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool backend_kind_from_token(
            std::string_view token, BackendKind &out) noexcept {
        for (const BackendKind kind : {BackendKind::Cve2026_43499, BackendKind::Cve2026_64560,
                                       BackendKind::Cve2026_31431, BackendKind::Cve2026_43503,
                                       BackendKind::Cve2026_23274, BackendKind::Cve2026_43284}) {
            if (token == backend_token_name(kind)) {
                out = kind;
                return true;
            }
        }
        return false;
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
