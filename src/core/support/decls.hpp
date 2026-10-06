#ifndef GHOSTLOCK_SUPPORT_DECLS_HPP
#define GHOSTLOCK_SUPPORT_DECLS_HPP

#include <cstddef>
#include "support/status.hpp"
#include <cstdint>
#include <sys/types.h>

/* Neutral support surface only. The 43499 heap prepare / leak / spray
 * declarations moved to backend/cve_2026_43499/spray.hpp (F15 ownership move),
 * so a neutral layer including this header names nothing backend-owned. */
namespace ghostlock::support {
    void read_first_line(const char *path, char *buf, size_t len);

    void log_sync(void);

    [[noreturn]] void fail_stop_dirty_race(const char *reason,
                                           int32_t error_number) noexcept;

    void disable_rseq_for_thread(void);

    long futex_op(
        uint32_t *uaddr, int32_t op, uint32_t val,
        const void *timeout_or_value, uint32_t *uaddr2, uint32_t val3);

    long sched_setattr_tid(int32_t tid, int32_t nice_value);

    void put64(unsigned char *p, size_t off, uint64_t value);

    void put32(unsigned char *p, size_t off, uint32_t value);

    pid_t clone_child(void);

    int32_t open_memfd(pid_t child);

    void kill_child(pid_t child);

    int32_t clone_memfd(void);
} // namespace ghostlock::support

#endif
