/** @file tests/credential_image_test.cpp
 *  The credential copy, measured with the numbers a real device declares.
 *
 *  Sections: 1. the copy the device's own document describes comes out field by
 *  field; 2. a layout that would not fit, or whose fields fall outside the copy,
 *  is refused by name; 3. a refusal leaves the buffer exactly as it was.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "backend/cve_2026_43499/memory/credential_image.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::build_credential_image;
    using ghostlock::backend::cve_2026_43499::memory::CredentialLayout;
    using ghostlock::backend::cve_2026_43499::memory::credential_verdict_name;
    using ghostlock::backend::cve_2026_43499::memory::CredentialVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The device's own numbers, read from the document it produced. */
    CredentialLayout device_layout() {
        CredentialLayout layout;
        layout.copy_size = 176U;
        layout.usage_offset = 0U;
        layout.usage_value = 256U;
        layout.caps_offset = 48U;
        layout.caps_count = 3U;
        layout.caps_value = 2199023255551ULL;
        layout.ref_offset[0U] = 128U;
        layout.ref_offset[1U] = 136U;
        layout.ref_offset[2U] = 144U;
        layout.ref_offset[3U] = 152U;
        layout.ref_image[0U] = 18446743799011097216ULL;
        layout.ref_image[1U] = 18446743799012843792ULL;
        layout.ref_image[2U] = 18446743799011098608ULL;
        layout.ref_image[3U] = 18446743799011097384ULL;
        layout.ref_count = 4U;
        return layout;
    }

    std::uint32_t load32(const std::uint8_t *bytes, std::size_t offset) {
        std::uint32_t value = 0U;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }

    std::uint64_t load64(const std::uint8_t *bytes, std::size_t offset) {
        std::uint64_t value = 0U;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }

    /* ---- 1: the copy the device describes ---- */
    void the_device_copy_comes_out() {
        const CredentialLayout layout = device_layout();
        std::uint8_t buffer[256U];
        std::memset(buffer, 0xAA, sizeof(buffer));
        expect(build_credential_image(buffer, sizeof(buffer), layout, 0U) == CredentialVerdict::Ok,
               "the device's credential fits");
        expect(load32(buffer, 0U) == 256U, "the usage count lands at its offset");
        expect(load64(buffer, 48U) == 2199023255551ULL && load64(buffer, 64U) == 2199023255551ULL,
               "the capability value fills every slot the count declares");
        expect(load64(buffer, 128U) == layout.ref_image[0U] &&
                   load64(buffer, 152U) == layout.ref_image[3U],
               "and the four reference images land where the profile says");
        expect(buffer[16U] == 0U && buffer[175U] == 0U,
               "the rest of the copy is cleared, not left as it was");
        expect(buffer[176U] == 0xAAU, "and nothing past the copy was touched");
        /* The same copy placed further in. */
        std::uint8_t moved[512U];
        std::memset(moved, 0x55, sizeof(moved));
        expect(build_credential_image(moved, sizeof(moved), layout, 64U) == CredentialVerdict::Ok &&
                   load32(moved, 64U) == 256U && moved[63U] == 0x55U,
               "an offset places the copy without disturbing what precedes it");
    }

    /* ---- 2: what cannot be installed ---- */
    void impossible_layouts_are_refused() {
        CredentialLayout layout = device_layout();
        std::uint8_t buffer[256U];
        expect(build_credential_image(buffer, 100U, layout, 0U) == CredentialVerdict::NoRoom,
               "a credential that does not fit is refused");
        expect(std::string_view(credential_verdict_name(CredentialVerdict::NoRoom)) ==
                   std::string_view("budget-exceeded"),
               "and the refusal is named");
        expect(build_credential_image(buffer, sizeof(buffer), layout, 200U) == CredentialVerdict::NoRoom,
               "nor does one that would run past the end");

        CredentialLayout outside = device_layout();
        outside.ref_offset[3U] = 200U;
        expect(build_credential_image(buffer, sizeof(buffer), outside, 0U) ==
                   CredentialVerdict::LayoutInvalid,
               "a field beyond the copy the profile declared is refused");
        CredentialLayout too_many = device_layout();
        too_many.ref_count = 5U;
        expect(build_credential_image(buffer, sizeof(buffer), too_many, 0U) ==
                   CredentialVerdict::TooManyReferences,
               "more references than the credential has are refused");
        CredentialLayout empty = device_layout();
        empty.copy_size = 0U;
        expect(build_credential_image(buffer, sizeof(buffer), empty, 0U) ==
                   CredentialVerdict::LayoutInvalid,
               "a copy of no size is not a credential");
    }

    /* ---- 3: a refusal writes nothing ---- */
    void refusals_write_nothing() {
        CredentialLayout layout = device_layout();
        layout.ref_offset[0U] = 300U;
        std::uint8_t buffer[256U];
        std::memset(buffer, 0xC3, sizeof(buffer));
        expect(build_credential_image(buffer, sizeof(buffer), layout, 0U) !=
                   CredentialVerdict::Ok,
               "the layout is refused");
        bool untouched = true;
        for (const std::uint8_t byte : buffer) {
            if (byte != 0xC3U) {
                untouched = false;
            }
        }
        expect(untouched, "and not one byte of the buffer was written");
        expect(build_credential_image(nullptr, 256U, device_layout(), 0U) ==
                   CredentialVerdict::LayoutInvalid,
               "no buffer is refused too");
    }
}  // namespace

int main() {
    std::printf("credential image: the device's copy\n");
    the_device_copy_comes_out();
    std::printf("credential image: impossible layouts\n");
    impossible_layouts_are_refused();
    std::printf("credential image: refusals\n");
    refusals_write_nothing();
    if (g_failures != 0U) {
        std::printf("credential image: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("credential image: PASS (device copy + named refusals + all or nothing)\n");
    return 0;
}
