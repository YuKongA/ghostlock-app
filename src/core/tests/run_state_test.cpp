/* Host test for the status-record protocol helpers. The live stdin/stdout
 * channel is exercised on-device; here we lock the wire format and the ACK
 * matcher plus the enable/disable state machine. */
#include "support/run_state.hpp"

#include <cstdio>
#include <string>

namespace run_state = ghostlock::support::run_state;

int main() {
    const std::string marker(1, '\x1e');

    const std::string event = run_state::format_event("w3b", "in_progress");
    if (event != marker + "GLK_STATUS w3b in_progress\n") {
        std::fprintf(stderr, "bad event: %s", event.c_str());
        return 1;
    }

    if (!run_state::is_ack(marker + "GLK_STATUS_ACK")) return 1;
    if (run_state::is_ack("GLK_STATUS_ACK")) return 1;
    if (run_state::is_ack(marker + "GLK_STATUS w3b in_progress")) return 1;

    run_state::configure(false);
    if (run_state::enabled()) return 1;
    run_state::configure(true);
    if (!run_state::enabled()) return 1;
    run_state::configure(false);

    std::puts("run_state_test: ok");
    return 0;
}
