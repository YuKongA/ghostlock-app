#ifndef GHOSTLOCK_TESTS_FAKE_BACKEND_STUB_HPP
#define GHOSTLOCK_TESTS_FAKE_BACKEND_STUB_HPP

/* ADR-0004 R8 composability probe (A2-5-5): a second backend that shares no
 * primitive, no route and no State with cve_2026_43499, instantiated through the
 * production Pipeline<Backend, Terminal> template. Host-only: it is never part
 * of the device build and never touches a device.
 *
 * Why it exists: Pipeline is the composition root, and the model is only
 * composable if an arbitrary (Backend, Terminal) pair that satisfies the
 * contracts can be plugged in without editing pipeline. FakeBackend binds its
 * own Document section through the neutral profile::Schema, keeps its own State
 * in the CoreSession opaque slot, and returns Continue; FakeTerminal consumes
 * its own Input and returns Done. The pair deliberately reuses one catalogued
 * backend id (Cve2026_43284 + PageCacheWrite + UmhForward) only because
 * Pipeline::target static_asserts against the sparse catalogue; the kind is an
 * id, not a dependency: FakeBackend includes none of the production backend
 * code.
 *
 * The test forces Pipeline::run to instantiate by taking its address (see
 * fake_pipeline_run) and then exercises the real assembly, so the proof is both
 * compile-time and runtime.
 */

#include "contract/identity.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/pipeline.hpp"
#include "profile/schema.hpp"
#include "session/core_session.hpp"
#include "contract/stage_result.hpp"
#include "terminal/terminal_input.hpp"

#include <array>
#include <cstdint>
#include <new>
#include <type_traits>

namespace ghostlock::tests::fake_backend {

    /* Call-order ledger. The test resets it before each Pipeline::run and reads
     * it afterwards; the fake components own no other observable state. */
    struct Trace final {
        int constructs = 0;
        int binds = 0;
        int backend_runs = 0;
        int terminal_runs = 0;
        int destroys = 0;
        bool state_visible_in_run = false;
        std::uint32_t terminal_input = 0;
    };
    inline Trace trace{};

    /* FakeBackend's own private State: a magic plus its bound payload. It is
     * stored in CoreSession::backend_state, exactly like a production backend,
     * but it is a distinct type with a distinct layout. */
    struct FakeState final {
        std::uint32_t magic = 0;
        std::uint32_t payload = 0;
        bool bound = false;
    };
    inline constexpr std::uint32_t kFakeStateMagic = 0x474c4b46U; /* "GLKF" */

    /* Its own owner schema/section. This is a second, independent owner of the
     * neutral Document; it shares no (section, key) with any production owner. */
    struct FakeView final {
        std::uint32_t payload = 0;
    };

    struct FakeSchema final {
        using View = FakeView;
        static constexpr std::array<profile::FieldSpec<FakeView>, 1> kFields{{
                {"fake.demo", "payload", 4U, false, true,
                 [](FakeView &view, std::uint64_t raw) noexcept {
                     view.payload = static_cast<std::uint32_t>(raw);
                 }},
        }};
    };
    static_assert(profile::SchemaDefinition<FakeSchema>);

    /* A terminal input that shares nothing with RootedChild or UmhForwardInput. */
    struct FakeTerminalInput final : terminal::TerminalInput {
        std::uint32_t forwarded = 0;
    };

    struct FakeBackend final {
        static constexpr contract::BackendKind kind = contract::BackendKind::Cve2026_43284;
        static constexpr contract::StepSetKind steps =
                contract::StepSetKind::PageCacheWrite;

        using State = FakeState;

        static FakeState *state_of(session::CoreSession &exploit_session) noexcept {
            return reinterpret_cast<FakeState *>(exploit_session.backend_state);
        }

        static void destroy_in_slot(void *slot) noexcept {
            static_cast<FakeState *>(slot)->~FakeState();
        }

        static void state_construct(session::CoreSession &exploit_session) noexcept {
            ::new (static_cast<void *>(exploit_session.backend_state))
                    FakeState{kFakeStateMagic, 0U, false};
            exploit_session.backend_state_ready = true;
            exploit_session.backend_state_dtor = &destroy_in_slot;
            ++trace.constructs;
        }

        static void state_destroy(session::CoreSession &exploit_session) noexcept {
            if (!exploit_session.backend_state_ready) return;
            state_of(exploit_session)->~FakeState();
            exploit_session.backend_state_ready = false;
            exploit_session.backend_state_dtor = nullptr;
            ++trace.destroys;
        }

        [[nodiscard]] static profile::BindStatus state_from(
                session::CoreSession &exploit_session, const profile::Document &document) {
            ++trace.binds;
            FakeState *state = state_of(exploit_session);
            if (state->magic != kFakeStateMagic) {
                return profile::BindStatus{profile::BindCode::Invalid, {}, {}};
            }
            FakeView view{};
            const profile::BindStatus status = profile::bind<FakeSchema>(document, view);
            if (!status.ok()) return status;
            state->payload = view.payload;
            state->bound = true;
            return {};
        }

        [[nodiscard]] static contract::StageResult run(
                session::CoreSession &exploit_session, const char *, bool,
                FakeTerminalInput &out) {
            ++trace.backend_runs;
            const FakeState *state = state_of(exploit_session);
            trace.state_visible_in_run =
                    state->magic == kFakeStateMagic && state->bound;
            out.forwarded = state->payload;
            return contract::StageResult::Continue;
        }
    };

    struct FakeTerminal final {
        static constexpr contract::TerminalKind kind = contract::TerminalKind::UmhForward;
        using Input = FakeTerminalInput;
        static constexpr terminal::ActivationContext activation =
                terminal::ActivationContext::KernelSpawned;

        [[nodiscard]] static contract::StageResult run(session::CoreSession &, Input &input) {
            ++trace.terminal_runs;
            trace.terminal_input = input.forwarded;
            return contract::StageResult::Done;
        }
    };

    /* The composability claim, checked at compile time by the production
     * template itself: a fresh backend/terminal pair satisfies the concepts and
     * lands on a catalogued dispatch target. */
    using FakePipeline = pipeline::Pipeline<FakeBackend, FakeTerminal>;
    static_assert(FakePipeline::target ==
                  pipeline::DispatchTarget::Cve43284PageCache_UmhForward);
    static_assert(contract::BackendIdentity<FakeBackend>);
    static_assert(contract::BackendExecution<FakeBackend, FakeTerminal::Input>);
    static_assert(contract::BackendState<FakeBackend>);
    static_assert(contract::TerminalIdentity<FakeTerminal>);
    static_assert(contract::TerminalExecution<FakeTerminal>);
    static_assert(std::is_same_v<FakeBackend::State, FakeState>);

    /* ODR-use Pipeline::run so its body (state_construct -> state_from -> run ->
     * Terminal::run -> state_destroy) is instantiated for the fake pair. */
    using PipelineRun = pipeline::RunResult (*)(session::CoreSession &,
                                                const profile::Document &, const char *,
                                                bool);
    inline PipelineRun fake_pipeline_run = &FakePipeline::run;

} // namespace ghostlock::tests::fake_backend

#endif
