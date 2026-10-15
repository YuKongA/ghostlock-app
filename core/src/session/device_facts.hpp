#pragma once
/** @file session/device_facts.hpp
 *  What the runtime knows about the device it is running on.
 *
 *  The kernel release is the one fact everything else follows from: the profile is
 *  matched by it and the module label is derived from it. It is read through a
 *  source type the way the race reads its syscalls through a landing, so a host test
 *  can drive the shape with facts it chooses and the device build binds the real
 *  ones.
 *
 *  Nothing here decides anything: it collects, and refuses to pretend. A fact that
 *  is not there is reported as not there, never as an empty string that later
 *  looks like a value.
 */
#include <cstddef>

namespace ghostlock::session {
    /** The facts the runtime reads once, at the start. */
    struct DeviceFacts final {
        /** The kernel release, exactly as uname reports it. */
        char release[96] = {};
        /** The Android release, when the platform reports one. */
        char android_release[16] = {};
        /** The primary ABI, when the platform reports one. */
        char abi[24] = {};
        bool has_release = false;
        bool has_android_release = false;
        bool has_abi = false;
    };

    /** Copy a C string into a bounded field, reporting whether it fit. */
    [[nodiscard]] inline bool copy_fact(char *out, std::size_t capacity,

                                       const char *text) noexcept {
        if (out == nullptr || capacity == 0U || text == nullptr || text[0U] == '\0') {
            return false;
        }
        std::size_t at = 0U;
        while (text[at] != '\0') {
            if (at + 1U >= capacity) {
                out[0U] = '\0';
                return false;
            }
            out[at] = text[at];
            ++at;
        }
        out[at] = '\0';
        return true;
    }

    /** Read the facts from a source. The source supplies two calls, so a test
     *  can answer with anything and the device build answers with the kernel. */
    template <class Source>
    [[nodiscard]] inline DeviceFacts read_device_facts() noexcept {
        DeviceFacts facts{};
        facts.has_release = copy_fact(facts.release, sizeof(facts.release), Source::release());
        facts.has_android_release = copy_fact(facts.android_release,
                                              sizeof(facts.android_release),

                                              Source::android_release());
        facts.has_abi = copy_fact(facts.abi, sizeof(facts.abi), Source::abi());
        return facts;
    }
}  // namespace ghostlock::session
