#include "support/run_state.hpp"

#include <cstdio>
#include <cstring>
#include <poll.h>
#include <unistd.h>

namespace ghostlock::support::run_state {
    namespace {
        constexpr char kMarker = static_cast<char>(0x1e);
        constexpr const char *kAckLine = "\x1eGLK_STATUS_ACK";
        constexpr int kAckTimeoutMs = 5000;
        constexpr size_t kMaxLine = 64;

        bool g_enabled = false;

        void emit(const char *step, const char *status) noexcept {
            const std::string line = format_event(step, status);
            std::fwrite(line.data(), 1, line.size(), stdout);
            std::fflush(stdout);
        }

        /* Reads one newline-terminated line from stdin with a bounded wait and
         * reports whether it is the app's ACK marker. */
        bool wait_ack() noexcept {
            char line[kMaxLine];
            size_t used = 0;
            while (true) {
                struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
                if (poll(&pfd, 1, kAckTimeoutMs) <= 0) return false;
                char c = '\0';
                if (read(STDIN_FILENO, &c, 1) <= 0) return false;
                if (c == '\n') break;
                if (used + 1 >= sizeof(line)) return false;
                line[used++] = c;
            }
            line[used] = '\0';
            return is_ack(line);
        }

        /* The status channel is best-effort: a missing ACK disables it for the
         * rest of the run instead of stalling the attack. */
        void disable_on_timeout() noexcept {
            std::fprintf(stderr,
                         "warning: status record ACK timeout; disabling status record "
                         "for this run (rerun without --enable-status-record)\n");
            std::fprintf(stdout, "%cGLK_STATUS_DISABLED\n", kMarker);
            std::fflush(stdout);
            g_enabled = false;
        }

        void transition(const char *step, const char *status) noexcept {
            if (!g_enabled || step == nullptr) return;
            emit(step, status);
            if (!wait_ack()) disable_on_timeout();
        }
    } // namespace

    std::string format_event(std::string_view step, std::string_view status) {
        std::string line;
        line.reserve(step.size() + status.size() + 24);
        line.push_back(kMarker);
        line.append("GLK_STATUS ");
        line.append(step.data(), step.size());
        line.push_back(' ');
        line.append(status.data(), status.size());
        line.push_back('\n');
        return line;
    }

    bool is_ack(std::string_view line) { return line == kAckLine; }

    void configure(bool enabled) noexcept { g_enabled = enabled; }

    bool enabled() noexcept { return g_enabled; }

    void enter(const char *step) noexcept { transition(step, "in_progress"); }

    void complete(const char *step) noexcept { transition(step, "completed"); }
} // namespace ghostlock::support::run_state
