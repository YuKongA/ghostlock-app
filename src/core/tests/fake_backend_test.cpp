/* Host test for the R8 fake backend (A2-5-5): a second backend/terminal pair
 * assembled through the real Pipeline<Backend, Terminal>, with a different
 * primitive, route and State. It proves composability at compile time (concept
 * static_asserts in the stub) and at run time (the state lifecycle and stage
 * order). No device, no route and no production backend code are linked. */

#include "tests/fake_backend_stub.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace ghostlock;
using namespace ghostlock::tests::fake_backend;

namespace {

    profile::Document document_with_payload(std::uint32_t payload) {
        profile::Document document;
        document.append_section("fake.demo").add("payload", payload);
        return document;
    }

} // namespace

int32_t main(void) {
    /* Happy path: construct -> bind -> backend run (Continue) -> terminal run
     * (Done), and the state guard destroys on exit. */
    trace = Trace{};
    {
        session::CoreSession session;
        const profile::Document document = document_with_payload(0x12345678U);
        const pipeline::RunResult result =
                fake_pipeline_run(session, document, "host", false);
        assert(result.code == pipeline::RunCode::Completed);
        assert(result.stage == pipeline::RunStage::Terminal);
        assert(trace.constructs == 1);
        assert(trace.binds == 1);
        assert(trace.backend_runs == 1);
        assert(trace.terminal_runs == 1);
        assert(trace.destroys == 1);
        assert(trace.state_visible_in_run);
        assert(trace.terminal_input == 0x12345678U);
        /* The guard released the opaque slot, so CoreSession teardown is inert. */
        assert(!session.backend_state_ready);
    }

    /* Fail-closed: a Document missing FakeBackend's required field is rejected
     * before the backend step or the terminal runs, and the guard still tears
     * the state down. */
    trace = Trace{};
    {
        session::CoreSession session;
        profile::Document empty;
        const pipeline::RunResult result = fake_pipeline_run(session, empty, "host", false);
        assert(result.code == pipeline::RunCode::Rejected);
        assert(result.stage == pipeline::RunStage::Backend);
        assert(trace.constructs == 1);
        assert(trace.binds == 1);
        assert(trace.backend_runs == 0);
        assert(trace.terminal_runs == 0);
        assert(trace.destroys == 1);
    }

    /* A second run reuses the same CoreSession slot: state_destroy left it
     * inert, and state_construct re-arms it. */
    trace = Trace{};
    {
        session::CoreSession session;
        const profile::Document document = document_with_payload(0xABCDEF01U);
        assert(fake_pipeline_run(session, document, "host", false).code ==
               pipeline::RunCode::Completed);
        trace = Trace{};
        assert(fake_pipeline_run(session, document, "host", false).code ==
               pipeline::RunCode::Completed);
        assert(trace.constructs == 1);
        assert(trace.destroys == 1);
    }

    std::puts("fake_backend_test: ok");
    return 0;
}
