#include "route/sigreturn_stamp.h"

#include <cstdio>
#include <cstring>

using namespace lsprivilege;

namespace {
    constexpr size_t kUcStackFlags = 0x90 + 8;
    constexpr size_t kUcSigmask = 0xa8;
    constexpr size_t kRecords = 0x250;

    uint32_t load32(const uint8_t *p) {
        uint32_t v;
        memcpy(&v, p, sizeof(v));
        return v;
    }

    uint64_t load64(const uint8_t *p) {
        uint64_t v;
        memcpy(&v, p, sizeof(v));
        return v;
    }
} // namespace

int32_t main(void) {
    /* Distinct-content stamp: vregs[i] = i pattern, fpsr/fpcr tagged. */
    unsigned char stamp[route::kSigStampDeliveredBytes]{};
    for (size_t i = 0; i < 512; ++i) stamp[i] = static_cast<unsigned char>(i * 7 + 3);
    stamp[512] = 0xef;
    stamp[513] = 0xbe;
    stamp[516] = 0xad;
    stamp[517] = 0xde;

    alignas(16) uint8_t frame[route::kSigStampFrameBytes];
    memset(frame, 0xa5, sizeof(frame));
    const uint64_t blocked = 0x0000c0ffee001122ULL;
    route::build_sigreturn_stamp_frame(frame, stamp, sizeof(stamp), blocked);

    /* uc_stack.ss_flags = SS_DISABLE; uc_sigmask carried verbatim. */
    if (load32(frame + kUcStackFlags) != 2) return 1;
    if (load64(frame + kUcSigmask) != blocked) return 1;

    /* fpsimd record: exact magic/size, fpsr/fpcr ahead of the vreg copy. */
    if (load32(frame + kRecords + 0) != 0x46508001u) return 1;
    if (load32(frame + kRecords + 4) != 0x210u) return 1;
    if (load32(frame + kRecords + 8) != 0x0000beefu) return 1;
    if (load32(frame + kRecords + 12) != 0x0000deadu) return 1;
    if (memcmp(frame + kRecords + 16, stamp, 512) != 0) return 1;

    /* END record and everything outside the planted slots stays zero. */
    if (load32(frame + kRecords + 0x210) != 0 ||
        load32(frame + kRecords + 0x214) != 0)
        return 1;
    for (size_t i = 0; i < 0x90; ++i)
        if (frame[i] != 0) return 1;

    /* An undersized stamp refuses without touching the frame. */
    memset(frame, 0xa5, sizeof(frame));
    route::build_sigreturn_stamp_frame(frame, stamp, 64, blocked);
    for (size_t i = 0; i < sizeof(frame); ++i)
        if (frame[i] != 0xa5) return 1;

    /* A larger stamp is truncated to the 520 delivered bytes. */
    unsigned char big[600];
    memset(big, 0x11, sizeof(big));
    memset(frame, 0, sizeof(frame));
    route::build_sigreturn_stamp_frame(frame, big, sizeof(big), 0);
    if (memcmp(frame + kRecords + 16, big, 512) != 0) return 1;

    puts("sigreturn stamp frame vectors passed");
    return 0;
}
