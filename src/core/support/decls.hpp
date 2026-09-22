#ifndef GHOSTLOCK_SUPPORT_DECLS_HPP
#define GHOSTLOCK_SUPPORT_DECLS_HPP

#include <cstddef>
#include "support/status.hpp"
#include <cstdint>
#include <sys/types.h>

#include "memory/payload_builder.h"

namespace ghostlock::support {
    void read_first_line(const char *path, char *buf, size_t len);

    void log_startup_context(void);

    void log_sync(void);

    void disable_rseq_for_thread(void);

    void init_p0_profile(void);

    long futex_op(
        uint32_t *uaddr, int32_t op, uint32_t val,
        const void *timeout_or_value, uint32_t *uaddr2, uint32_t val3);

    long sched_setattr_tid(int32_t tid, int32_t nice_value);

    void put64(unsigned char *p, size_t off, uint64_t value);

    void put32(unsigned char *p, size_t off, uint32_t value);

    pid_t clone_child(void);

    pid_t clone_leak_child(void);

    int32_t open_memfd(pid_t child);

    void kill_child(pid_t child);

    void close_reclaim_sockets(void);

    int32_t quarantine_reclaim_sockets(void);

    void release_quarantined_reclaim_sockets(void);

    Status stash_prebuilt_page(void);

    Status activate_prebuilt_page(void);

    void discard_prebuilt_page(void);

    void cleanup_page_prepare_state(void);

    int32_t clone_memfd(void);

    void prepare_ctxs(void);

    int32_t prepare_skb_payload(uintptr_t base, const ghostlock::memory::WriteRequest *request);

    uintptr_t prepare_kernel_page(const ghostlock::memory::WriteRequest *request);

    uintptr_t prepare_good_kernel_page(const ghostlock::memory::WriteRequest &request);
} // namespace ghostlock::support

#endif
