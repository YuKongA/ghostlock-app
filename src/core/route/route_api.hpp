#ifndef LSPRIVILEGE_ROUTE_API_HPP
#define LSPRIVILEGE_ROUTE_API_HPP

#include <cstdint>
#include "support/status.hpp"
#include <sys/select.h>

#include "memory/payload_builder.h"
#include "route/route_status.h"

namespace lsprivilege::session {
    struct ExploitSession;
} // namespace lsprivilege::session

namespace lsprivilege::route {
    void fdset_put_word(fd_set *set, int32_t word, uint64_t value);

    uint64_t fdset_get_word(const fd_set *set, int32_t word);

    void reserve_standard_io(void);

    RouteStatus do_pselect_fake_lock_route(const lsprivilege::memory::WriteRequest *request);

    RouteStatus do_tcp_fake_lock_route(const lsprivilege::memory::WriteRequest *request);

    RouteStatus do_kernel5_fake_lock_route(const lsprivilege::memory::WriteRequest *request);
} // namespace lsprivilege::route

#endif
