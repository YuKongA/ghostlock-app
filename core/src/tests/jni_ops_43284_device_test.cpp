/** @file tests/jni_ops_43284_device_test.cpp
 *  The two 43284 ops that need no device: the bytes this build carries and the hook plan.
 *
 *  Their job in the boundary is to hand the page-cache chain its own images; their job here is to
 *  pin, on the host, exactly what a device must see coming back through JNI -- the sizes, the blob
 *  digests recorded in the blobs' own headers, and the hook numbers the plan derives.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "backend/cve_2026_43284/embed/libcxx_blob.hpp"
#include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"
#include "jni/ops_43284.hpp"
#include "support/sha256.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::embed::kLibcxxPaddedSize;
    using ghostlock::backend::cve_2026_43284::embed::kLibcxxSha256;
    using ghostlock::backend::cve_2026_43284::embed::kSpliceHelperPaddedSize;
    using ghostlock::backend::cve_2026_43284::embed::kSpliceHelperSha256;
    using ghostlock::jni::device_seams_43284;
    using ghostlock::jni::Ops43284Seams;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_helper_image_is_the_documented_one() {
        Ops43284Seams seams = device_seams_43284();
        expect(seams.embedded_image != nullptr, "the image seam exists");
        std::span<const std::byte> image{};
        expect(seams.embedded_image(0U, image, nullptr), "role 0 is served");
        expect(image.size() == kSpliceHelperPaddedSize, "the helper is served padded to the write block");
        const auto digest = ghostlock::support::sha256(
            reinterpret_cast<const std::uint8_t *>(image.data()), image.size());
        char hex[65] = {};
        ghostlock::support::sha256_hex(digest, hex);
        std::printf("device seam: splice_helper padded=%zu sha256=%s\n", image.size(), hex);

        /* The header records the digest of the unpadded helper; the padded tail is zero, so both
         *  facts are checked: the bytes are the documented build, and the padding is the block. */
        const auto unpadded = ghostlock::support::sha256(
            reinterpret_cast<const std::uint8_t *>(image.data()), image.size() - 8U);
        expect(ghostlock::support::sha256_matches_text(unpadded, kSpliceHelperSha256),
               "the first 1432 bytes are the helper the header documents");
        bool tail_zero = true;
        for (std::size_t i = image.size() - 8U; i < image.size(); ++i) {
            tail_zero = tail_zero && image[i] == std::byte{0};
        }
        expect(tail_zero, "the pad to the block is zeros");
    }

    void the_landing_payload_carries_the_two_words() {
        using namespace ghostlock::backend::cve_2026_43284;
        Ops43284Seams seams = device_seams_43284();
        std::span<const std::byte> payload{};
        expect(seams.embedded_image(1U, payload, nullptr), "role 1 is served");
        expect(payload.size() == kLibcxxPaddedSize, "the landing payload is the blob, padded to the block");
        const auto blob = ghostlock::support::sha256(
            reinterpret_cast<const std::uint8_t *>(embed::libcxx_bytes()), embed::libcxx_size());
        expect(ghostlock::support::sha256_matches_text(blob, kLibcxxSha256),
               "the blob this build carries is the documented one");

        const hook::SentryPlan plan = hook::device_sentry_plan();
        std::uint32_t branch = 0U;
        expect(hook::hook_branch(plan, branch) == hook::SentryError::Ok, "the branch word derives");
        std::span<const std::byte> hook_payload{};
        std::uint64_t branch_word = 0U;
        std::uint64_t displaced_word = 0U;
        std::uint64_t site_offset = 0U;
        std::uint64_t landing_offset = 0U;
        expect(seams.hook_words(branch_word, displaced_word, site_offset, landing_offset, hook_payload,
                                nullptr),
               "the hook words are served");
        expect(branch_word == branch, "the branch word is the plan branch");
        expect(displaced_word == plan.displaced_instruction, "the displaced word is the plan word");
        expect(site_offset == plan.hook_file_offset && landing_offset == plan.payload_file_offset,
               "both offsets are the plan file offsets");
        expect(site_offset != landing_offset, "the site and the landing room are different places");
        expect(hook_payload.size() == payload.size(), "the hook payload is the same image");
        expect(std::memcmp(hook_payload.data(), payload.data(), payload.size()) == 0,
               "and byte for byte the same bytes");
        char hex[65] = {};
        ghostlock::support::sha256_hex(
            ghostlock::support::sha256(reinterpret_cast<const std::uint8_t *>(payload.data()),
                                       payload.size()),
            hex);
        std::printf("device seam: landing_payload padded=%zu sha256=%s branch=0x%x displaced=0x%llx\n",
                    payload.size(), hex, static_cast<unsigned>(branch_word),
                    static_cast<unsigned long long>(displaced_word));
    }

    void the_seams_that_need_a_device_stay_absent() {
        Ops43284Seams seams = device_seams_43284();
        expect(seams.resolve == nullptr && seams.write_block == nullptr && seams.read_block == nullptr &&
                   seams.trigger == nullptr && seams.wait_terminal == nullptr,
               "the device work is not claimed until it is implemented (J4b)");
    }
}  // namespace

int main() {
    std::printf("device seam 43284: helper image\n");
    the_helper_image_is_the_documented_one();
    std::printf("device seam 43284: landing payload\n");
    the_landing_payload_carries_the_two_words();
    std::printf("device seam 43284: what stays unclaimed\n");
    the_seams_that_need_a_device_stay_absent();
    if (g_failures != 0U) {
        std::printf("device seam 43284: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("device seam 43284: PASS (images and hook plan)\n");
    return 0;
}
