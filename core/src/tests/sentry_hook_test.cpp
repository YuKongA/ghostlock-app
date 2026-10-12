/** @file tests/sentry_hook_test.cpp
 *  The sentry hook's arithmetic, decoded rather than compared.
 *
 *  What a wrong number here costs is a device that stops responding, so the test does not trust the
 *  constants it is given: it decodes the branch words back into targets and checks that they land
 *  where the plan says (the payload's entry, and the instruction after the site), and it checks the
 *  payload's own two words against the plan as well. The refusals matter as much: a payload that
 *  does not fit the landing room, a branch out of range and a buffer too small must all be named
 *  rather than written.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43284/embed/libcxx_blob.hpp"
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::embed::kLibcxxJumpBackOffset;
    using ghostlock::backend::cve_2026_43284::embed::kLibcxxPaddedSize;
    using ghostlock::backend::cve_2026_43284::hook::build_sentry_payload;
    using ghostlock::backend::cve_2026_43284::hook::device_sentry_plan;
    using ghostlock::backend::cve_2026_43284::hook::hook_branch;
    using ghostlock::backend::cve_2026_43284::hook::SentryError;
    using ghostlock::backend::cve_2026_43284::hook::SentryPlan;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The target a B instruction branches to. */
    std::uint64_t branch_target(std::uint64_t from, std::uint32_t word) noexcept {
        std::int32_t imm = static_cast<std::int32_t>(word & 0x03FFFFFFU);
        if ((imm & 0x02000000) != 0) {
            imm |= static_cast<std::int32_t>(0xFC000000U);
        }
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(from) +
                                          static_cast<std::int64_t>(imm) * 4);
    }

    std::uint32_t word_at(const std::uint8_t *bytes, std::size_t at) noexcept {
        return static_cast<std::uint32_t>(bytes[at]) |
               (static_cast<std::uint32_t>(bytes[at + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[at + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[at + 3U]) << 24U);
    }

    void the_plan_and_its_two_branches_land_where_they_say() {
        const SentryPlan plan = device_sentry_plan();
        expect(plan.hook_vaddr == 0xB70ACU && plan.hook_file_offset == 0xB30ACU,
               "the site is the instruction after the guard the plan skipped");
        expect(plan.guard_skipped && plan.guard_instruction == 0xD503245FU, "the guard is bti c");
        expect(plan.displaced_instruction != 0U, "and the word it displaced is known");
        expect(plan.payload_max_bytes == 0x9D8U, "the landing room is the page tail the run reported");

        std::uint32_t branch = 0U;
        expect(hook_branch(plan, branch) == SentryError::Ok, "the site branch encodes");
        expect(branch_target(plan.hook_vaddr, branch) == plan.payload_vaddr + plan.entry_offset,
               "and lands on the payload's entry, decoded rather than compared");

        std::uint8_t payload[kLibcxxPaddedSize * 2U] = {};
        std::size_t size = 0U;
        expect(build_sentry_payload(plan, payload, sizeof(payload), size) == SentryError::Ok,
               "the payload builds");
        expect(size == kLibcxxPaddedSize, "and is the blob padded to the block");
        expect(word_at(payload, plan.displaced_offset) == plan.displaced_instruction,
               "the payload replays the word it displaced");
        const std::uint32_t jump = word_at(payload, plan.jump_back_offset);
        expect(branch_target(plan.payload_vaddr + plan.jump_back_offset, jump) == plan.hook_vaddr + 4U,
               "and branches back to the instruction after the site, decoded");
        /* The two words the payload must not carry from the template: the template's own placeholders
         * would send init into whatever the upstream build assumed. */
        expect(word_at(payload, kLibcxxJumpBackOffset) == jump, "the jump-back word is the one bound");
    }

    void what_cannot_be_written_is_refused() {
        SentryPlan plan = device_sentry_plan();
        std::uint8_t payload[kLibcxxPaddedSize * 2U] = {};
        std::size_t size = 0U;
        expect(build_sentry_payload(plan, payload, kLibcxxPaddedSize - 1U, size) ==
                   SentryError::OutputTooSmall,
               "a buffer smaller than the payload is refused");
        SentryPlan no_room = plan;
        no_room.payload_max_bytes = 16U;
        expect(build_sentry_payload(no_room, payload, sizeof(payload), size) == SentryError::NoRoom,
               "and a landing room smaller than the payload is refused");
        SentryPlan far = plan;
        /* The branch field reaches +/-128 MiB, so the refusal is at the limit and not before it. */
        far.payload_vaddr = plan.hook_vaddr + 0x8000000U;
        std::uint32_t branch = 0U;
        expect(hook_branch(far, branch) == SentryError::BranchOutOfRange,
               "a payload further than the branch can reach is refused");
        SentryPlan misaligned = plan;
        misaligned.payload_vaddr = plan.payload_vaddr + 2U;
        expect(build_sentry_payload(misaligned, payload, sizeof(payload), size) ==
                   SentryError::BranchOutOfRange,
               "and an unaligned landing site is refused rather than rounded");
        SentryPlan inside = plan;
        inside.jump_back_offset = kLibcxxPaddedSize;
        expect(build_sentry_payload(inside, payload, sizeof(payload), size) ==
                   SentryError::InvalidPlan,
               "a jump-back word outside the blob is refused");
    }
}  // namespace

int main() {
    std::printf("sentry hook: the plan and its branches\n");
    the_plan_and_its_two_branches_land_where_they_say();
    std::printf("sentry hook: what cannot be written\n");
    what_cannot_be_written_is_refused();
    if (g_failures != 0U) {
        std::printf("sentry hook: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("sentry hook: PASS (branches decoded to their targets; refusals named)\n");
    return 0;
}
