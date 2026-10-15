#pragma once
/** @file session/device_facts_linux.hpp
 *  The real device facts, read from the kernel and the platform properties.
 *
 *  Android only: it calls uname and reads system properties, neither of which the
 *  host has. Kept apart from the shape above for the same reason the futex landing is
 *  kept apart from the race: the shape is testable anywhere, this is provable on a
 *  device.
 */
#include <sys/utsname.h>

#include "session/device_facts.hpp"

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace ghostlock::session {
    struct LinuxDeviceFactsSource final {
        /** The kernel release from uname, or nullptr when it cannot be read. */
        static const char *release() noexcept {
            static char stored[96] = {};
            static bool read = false;
            if (!read) {
                struct utsname names {};
                if (::uname(&names) == 0) {
                    for (std::size_t i = 0U; i < sizeof(stored) - 1U; ++i) {
                        stored[i] = names.release[i];
                        if (names.release[i] == '\0') {
                            break;
                        }
                    }
                }
                read = true;
            }
            return stored[0U] == '\0' ? nullptr : stored;
        }

        /** The Android release, or nullptr when the platform does not report one. */
        static const char *android_release() noexcept {
#if defined(__ANDROID__)
            static char stored[PROP_VALUE_MAX] = {};
            if (__system_property_get("ro.build.version.release", stored) <= 0) {
                return nullptr;
            }
            return stored;
#else
            return nullptr;
#endif
        }

        /** The primary ABI, or nullptr when the platform does not report one. */
        static const char *abi() noexcept {
#if defined(__ANDROID__)
            static char stored[PROP_VALUE_MAX] = {};
            if (__system_property_get("ro.product.cpu.abi", stored) <= 0) {
                return nullptr;
            }
            return stored;
#else
            return nullptr;
#endif
        }
    };
}  // namespace ghostlock::session
