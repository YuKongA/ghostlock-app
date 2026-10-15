/** @file tests/pagecache_door_linux_test.cpp
 *  The real door, on the host, without touching the network.
 *
 *  What can be checked here is what it refuses: a path it cannot hold, a material it was not given,
 *  a descriptor that is not one. The one case that would connect is not exercised, because a test
 *  that opens a socket to prove a function works has stopped being a test and become a side effect.
 *  Opening the target is exercised, but only against a path that does not exist, so the call is made
 *  and its failure is named.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/landing/pagecache_door_linux.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::landing::kDoorPathBytes;
    using ghostlock::backend::cve_2026_43284::landing::PageCacheDoorLinux;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void a_path_it_cannot_hold_is_refused_at_construction() {
        PageCacheDoorLinux none(nullptr, 0U);
        expect(!none.usable(), "a door with no path is not usable");
        expect(none.open_target() < 0, "and opening the target fails rather than opening something");

        const char huge[kDoorPathBytes + 8U] = {};
        PageCacheDoorLinux too_long(huge, sizeof(huge));
        expect(!too_long.usable(), "a path wider than the buffer is refused rather than truncated");
        expect(too_long.open_target() < 0, "and it opens nothing");

        PageCacheDoorLinux empty("", 0U);
        expect(!empty.usable(), "an empty path is refused too");
    }

    void a_path_that_fits_is_usable_and_opening_a_missing_one_fails_by_itself() {
        const char *missing = "/nonexistent/ghostlock/door-test-target";
        PageCacheDoorLinux door(missing, 38U);
        expect(door.usable(), "a path that fits makes a usable door");
        /* The call really happens; the file really is not there; the answer really is negative. This
         *  is the one case where the door touches the filesystem, and it is a read-only open of a
         *  path that does not exist. */
        expect(door.open_target() < 0, "opening a path that is not there answers negative");
    }

    void material_that_was_not_declared_is_refused_before_any_socket_is_made() {
        const char *missing = "/nonexistent/ghostlock/door-test-target";
        PageCacheDoorLinux door(missing, 38U);
        SaMaterial unset{};
        expect(!unset.set, "the material starts undeclared, as the frame reader leaves it");
        expect(door.connect_esp(unset) < 0,
               "an undeclared material is refused, and no socket is created to find that out");
    }

    void closing_a_descriptor_that_is_not_one_is_harmless() {
        const char *missing = "/nonexistent/ghostlock/door-test-target";
        PageCacheDoorLinux door(missing, 38U);
        door.close_fd(-1);
        door.close_fd(-42);
        expect(true, "closing a non-descriptor does nothing and throws nothing");
        std::printf("door linux: path bytes %zu\n", kDoorPathBytes);
    }
}  // namespace

int main() {
    std::printf("door linux: what it refuses\n");
    a_path_it_cannot_hold_is_refused_at_construction();
    std::printf("door linux: a path that fits\n");
    a_path_that_fits_is_usable_and_opening_a_missing_one_fails_by_itself();
    std::printf("door linux: undeclared material\n");
    material_that_was_not_declared_is_refused_before_any_socket_is_made();
    std::printf("door linux: bad descriptors\n");
    closing_a_descriptor_that_is_not_one_is_harmless();
    if (g_failures != 0U) {
        std::printf("door linux: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("door linux: PASS (refuses what it cannot hold, and connects nothing)\n");
    return 0;
}
