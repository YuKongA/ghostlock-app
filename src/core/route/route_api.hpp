#ifndef GHOSTLOCK_ROUTE_API_HPP
#define GHOSTLOCK_ROUTE_API_HPP

#include <cstdint>
#include "support/status.hpp"
#include <sys/select.h>

#include "memory/payload_builder.h"
#include "route/route_status.h"

namespace ghostlock::session {
    struct ExploitSession;
} // namespace ghostlock::session

namespace ghostlock::route {
    void fdset_put_word(fd_set *set, int32_t word, uint64_t value);

    uint64_t fdset_get_word(const fd_set *set, int32_t word);

    void reserve_standard_io(void);

    RouteStatus do_pselect_fake_lock_route(const ghostlock::memory::WriteRequest *request);

    RouteStatus do_tcp_fake_lock_route(const ghostlock::memory::WriteRequest *request);

    RouteStatus do_kernel5_fake_lock_route(const ghostlock::memory::WriteRequest *request);

    Status kernel5_resident_start(void);

    Status kernel5_resident_write(uintptr_t target, uintptr_t value);

    void kernel5_resident_stop(void);
} // namespace ghostlock::route

#endif
