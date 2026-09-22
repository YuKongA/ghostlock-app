#include "routes/tcp_zerocopy_route.h"

#include <sys/mman.h>

#include <utility>

using namespace ghostlock;

TcpZerocopyRoute::TcpZerocopyRoute(
        PiRace *race_context, const WriteRequest *route_request,
        const execution_settings *execution_settings_value,
        size_t mapping_length_value) noexcept
    : race(race_context),
      request(route_request),
      execution(execution_settings_value),
      mapping_length(mapping_length_value) {
  /* C atomics cannot carry initializers in C++; mirror the old
   * tcp_zerocopy_route_context_init stores explicitly. */
  punch_go.store(0, std::memory_order_relaxed);
  punch_stop.store(0, std::memory_order_relaxed);
  punch_phase.store(0, std::memory_order_relaxed);
  punch_failed.store(0, std::memory_order_relaxed);
  status.code = ROUTE_RETRYABLE;
}

TcpZerocopyRoute::TcpZerocopyRoute(
        TcpZerocopyRoute &&other) noexcept
    : race(other.race),
      request(other.request),
      execution(other.execution),
      client_fd(std::move(other.client_fd)),
      server_fd(std::move(other.server_fd)),
      punch_fd(std::move(other.punch_fd)),
      mapping(std::move(other.mapping)),
      mapping_length(other.mapping_length),
      page_size(other.page_size),
      punch_worker(std::move(other.punch_worker)),
      route_won(other.route_won),
      status(other.status) {
  punch_go.store(other.punch_go.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
  punch_stop.store(other.punch_stop.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
  punch_phase.store(other.punch_phase.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
  punch_failed.store(other.punch_failed.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
}

int TcpZerocopyRoute::fail(
        int step, int error_number) noexcept {
  status.step = step;
  status.error_number = error_number;
  return -1;
}

void TcpZerocopyRoute::disarm() noexcept {
  race->consumer_go.store(0);
  punch_go.store(0);
  punch_stop.store(1);
  while (race->consumer_inflight.load()) {
    __asm__ volatile("yield":: : "memory");
  }
  status.kernel_disarmed = 1;
}

void TcpZerocopyRoute::retain_for_process_lifetime() noexcept {
  (void) punch_fd.release_to_process_lifetime("tcp route dirty: puncher may run");
  (void) server_fd.release_to_process_lifetime("tcp route dirty: puncher may run");
  (void) client_fd.release_to_process_lifetime("tcp route dirty: puncher may run");
  (void) mapping.release();
  (void) punch_worker.release();
}

void TcpZerocopyRoute::destroy() noexcept {
  if (punch_worker.joinable()) {
    const int join_error = punch_worker.join();
    if (join_error != 0) {
      (void) fail(47, join_error);
      status.code = ROUTE_DIRTY_FAILURE;
      retain_for_process_lifetime();
      return;
    }
  }
  if (mapping.valid()) {
    void *mapped = mapping.release();
    if (munmap(mapped, mapping_length) != 0) {
      const int saved_errno = errno;
      (void) fail(48, saved_errno);
      status.code = ROUTE_DIRTY_FAILURE;
      retain_for_process_lifetime();
      return;
    }
    mapping = MappedRegion{};
  }
  punch_fd.reset();
  server_fd.reset();
  client_fd.reset();
  status.userspace_clean = 1;
  if (!route_won && status.kernel_disarmed) {
    status.code = ROUTE_FALLBACK_SAFE;
  }
}
