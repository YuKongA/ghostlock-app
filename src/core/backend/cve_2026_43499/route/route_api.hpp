#ifndef GHOSTLOCK_ROUTE_API_HPP
#define GHOSTLOCK_ROUTE_API_HPP

#include <cstdint>
#include "support/status.hpp"
#include <sys/select.h>

#include "memory/payload_builder.h"
#include "backend/cve_2026_43499/route/route_status.h"

namespace ghostlock::session {
    struct CoreSession;
} // namespace ghostlock::session

namespace ghostlock::backend::cve_2026_43499::route {
    void fdset_put_word(fd_set *set, int32_t word, uint64_t value);

    uint64_t fdset_get_word(const fd_set *set, int32_t word);

    void reserve_standard_io(void);

    RouteStatus do_pselect_fake_lock_route(const ghostlock::memory::WriteRequest *request);

    RouteStatus do_tcp_fake_lock_route(const ghostlock::memory::WriteRequest *request);

    RouteStatus do_kernel5_fake_lock_route(const ghostlock::memory::WriteRequest *request);
} // namespace ghostlock::backend::cve_2026_43499::route

#endif
