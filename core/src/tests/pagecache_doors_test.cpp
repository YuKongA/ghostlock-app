/** @file tests/pagecache_doors_test.cpp
 *  The doors, driven against a record of what was asked of the system.
 *
 *  Four behaviours, all of them from the measured rule that whoever opens a door closes it: both
 *  doors open once; closing closes exactly what this object opened; a descriptor the caller handed
 *  in is used and never closed here, because closing someone else's is a double close; and when the
 *  second door fails the first is given back, so a failed open never leaves a half-open route.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/landing/pagecache_doors.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::landing::DoorOps;
    using ghostlock::backend::cve_2026_43284::landing::PageCacheDoors;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    class RecordingOps final : public DoorOps {
    public:
        int opened = 0;
        int connected = 0;
        int closed[8] = {};
        int next_fd = 3;
        bool target_fails = false;
        bool socket_fails = false;

        [[nodiscard]] int open_target() noexcept override {
            if (target_fails) {
                return -1;
            }
            return next_fd++;
        }
        [[nodiscard]] int connect_esp(const SaMaterial &) noexcept override {
            if (socket_fails) {
                return -1;
            }
            return next_fd++;
        }
        void close_fd(int fd) noexcept override {
            if (fd >= 0 && fd < 8) {
                closed[fd] = 1;
            }
        }
        [[nodiscard]] int closed_count() const noexcept {
            int n = 0;
            for (int i = 0; i < 8; ++i) {
                n += closed[i];
            }
            return n;
        }
    };

    void both_doors_open_and_close_once() {
        RecordingOps ops;
        PageCacheDoors doors(ops);
        SaMaterial params;
        expect(doors.open(params), "both doors open");
        expect(doors.file_fd() >= 0 && doors.socket_fd() >= 0, "and both have a descriptor");
        expect(doors.owns_file(), "the file is this object's to close, since it opened it");
        expect(!doors.open(params), "a second open is refused");
        expect(doors.close(), "closing works");
        expect(ops.closed_count() == 2, "and closes exactly the two it opened");
        expect(!doors.close(), "a second close is refused");
    }

    void a_handed_in_descriptor_is_never_closed_here() {
        RecordingOps ops;
        PageCacheDoors doors(ops, 42);
        SaMaterial params;
        expect(doors.open(params), "a handed-in descriptor opens the file door");
        expect(doors.file_fd() == 42, "the one it was given");
        expect(!doors.owns_file(), "and it is not this object's to close");
        static_cast<void>(doors.close());
        expect(ops.closed_count() == 1, "so only the socket is closed, not the caller's descriptor");
    }

    void a_failed_second_door_gives_the_first_back() {
        RecordingOps ops;
        ops.socket_fails = true;
        PageCacheDoors doors(ops);
        SaMaterial params;
        expect(!doors.open(params), "the open fails when the socket cannot be made");
        expect(ops.closed_count() == 1, "and the file door it had opened is given back");
        expect(!doors.owns_file() && doors.file_fd() < 0, "leaving nothing half open");
    }
}  // namespace

int main() {
    std::printf("page cache doors: open and close\n");
    both_doors_open_and_close_once();
    std::printf("page cache doors: a handed-in descriptor\n");
    a_handed_in_descriptor_is_never_closed_here();
    std::printf("page cache doors: a failure leaves nothing half open\n");
    a_failed_second_door_gives_the_first_back();
    if (g_failures != 0U) {
        std::printf("page cache doors: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page cache doors: PASS (whoever opens closes, and nothing half open)\n");
    return 0;
}
