/** @file tests/device_facts_test.cpp
 *  The facts the runtime reads about its device, and the label they imply.
 *
 *  The source is a template parameter, so this drives the shape with facts it
 *  chooses -- including the release this device reports -- and checks the two
 *  refusals that matter: a fact too long to hold is reported as absent rather
 *  than truncated, and an absent fact is absent rather than empty.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "contract/kmi_catalog.hpp"
#include "session/device_facts.hpp"

namespace {
    using ghostlock::contract::kmi_of;
    using ghostlock::contract::label_for_release;
    using ghostlock::session::DeviceFacts;
    using ghostlock::session::read_device_facts;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The device this project is tested on, as uname reports it. */
    struct DeviceSource final {
        static const char *release() noexcept {
            return "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";
        }
        static const char *android_release() noexcept { return "15"; }
        static const char *abi() noexcept { return "arm64-v8a"; }
    };

    /** A device that reports no release at all. */
    struct SilentSource final {
        static const char *release() noexcept { return nullptr; }
        static const char *android_release() noexcept { return nullptr; }
        static const char *abi() noexcept { return nullptr; }
    };

    /** A release longer than any field can hold. */
    struct EndlessSource final {
        static const char *release() noexcept {
            return "5.15.189-android13-8-00016-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        }
        static const char *android_release() noexcept { return nullptr; }
        static const char *abi() noexcept { return nullptr; }
    };

    void the_facts_are_read() {
        const DeviceFacts facts = read_device_facts<DeviceSource>();
        expect(facts.has_release, "the release is read");
        expect(std::strcmp(facts.release, DeviceSource::release()) == 0,
               "exactly as the kernel reports it");
        expect(facts.has_android_release && std::strcmp(facts.android_release, "15") == 0,
               "and the platform release with it");
        expect(facts.has_abi && std::strcmp(facts.abi, "arm64-v8a") == 0, "and the abi");

        /* What those facts imply: the module this device would be delivered. */
        char label[64];
        expect(label_for_release(facts.release, label, sizeof(label)),
               "the release yields a label");
        expect(std::strcmp(label, "android13-5.15") == 0, "which is the one the catalogue has");
        expect(kmi_of(std::string_view(label)) != nullptr, "and this build can deliver it");
    }

    void an_absent_fact_is_absent() {
        const DeviceFacts none = read_device_facts<SilentSource>();
        expect(!none.has_release && none.release[0U] == '\0',
               "a device that reports nothing leaves the field empty");
        expect(!none.has_android_release && !none.has_abi, "and says so for each fact");
        const DeviceFacts endless = read_device_facts<EndlessSource>();
        expect(!endless.has_release && endless.release[0U] == '\0',
               "a fact too long to hold is absent, not truncated");
    }
}  // namespace

int main() {
    std::printf("device facts: reading them\n");
    the_facts_are_read();
    std::printf("device facts: when a fact is not there\n");
    an_absent_fact_is_absent();
    if (g_failures != 0U) {
        std::printf("device facts: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("device facts: PASS (read + joined to the label + absent means absent)\n");
    return 0;
}
