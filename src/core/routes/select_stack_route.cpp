#include "routes/select_stack_route.h"

#include <unistd.h>

#include <utility>

using namespace ghostlock;
using namespace ghostlock::profile;

SelectStackRoute::SelectStackRoute(
    PiRace *race_context, const WriteRequest *route_request,
    const execution_settings *execution_settings_value,
    SelectStackLayout route_layout,
    const int stdio_backup_value[3]) noexcept
    : race(race_context),
      request(route_request),
      execution(execution_settings_value),
      layout(route_layout) {
    for (int fd = 0; fd < 3; fd++) {
        stdio_backup[fd] =
                BorrowedFd(stdio_backup_value ? stdio_backup_value[fd] : -1);
    }
    /* The C version memset the whole context; zero the sets explicitly so a
   * destroy() before prepare() never tests uninitialized bits. */
    input_set.zero();
    output_set.zero();
    exception_set.zero();
    owned_input_set.zero();
    owned_output_set.zero();
    owned_exception_set.zero();
    status.code = ROUTE_RETRYABLE;
}

SelectStackRoute::SelectStackRoute(
    SelectStackRoute &&other) noexcept
    : race(other.race),
      request(other.request),
      execution(other.execution),
      layout(other.layout),
      input_set(other.input_set),
      output_set(other.output_set),
      exception_set(other.exception_set),
      owned_input_set(other.owned_input_set),
      owned_output_set(other.owned_output_set),
      owned_exception_set(other.owned_exception_set),
      pipe_read(std::move(other.pipe_read)),
      pipe_write(std::move(other.pipe_write)),
      block(std::move(other.block)),
      high_read(std::move(other.high_read)),
      block_borrows_pipe(other.block_borrows_pipe),
      selected_fds_installed(other.selected_fds_installed),
      consumer_stuck(other.consumer_stuck),
      calls(other.calls),
      successes(other.successes),
      select_result(other.select_result),
      select_errno(other.select_errno),
      status(other.status) {
    for (int fd = 0; fd < 3; fd++) {
        stdio_backup[fd] = other.stdio_backup[fd];
    }
}

int SelectStackRoute::fail(int step, int error_number) noexcept {
    status.step = step;
    status.error_number = error_number;
    return -1;
}

void SelectStackRoute::disarm() noexcept {
    race->consumer_go.store(0);
    if (race->consumer_inflight.load() != 0) {
        for (int i = 0;
             i < 2000 && race->consumer_inflight.load() != 0;
             i++) {
            usleep(1000);
        }
        consumer_stuck = race->consumer_inflight.load() != 0;
    }
    status.kernel_disarmed = !consumer_stuck;
}

void SelectStackRoute::retain_for_process_lifetime() noexcept {
    /* A stuck consumer may still walk these descriptors, so none of them may be
   * closed: hand every owner's descriptor to the process lifetime. */
    (void) high_read.release_to_process_lifetime("pselect consumer stuck");
    if (!block_borrows_pipe) {
        (void) block.release_to_process_lifetime("pselect consumer stuck");
    }
    (void) pipe_read.release_to_process_lifetime("pselect consumer stuck");
    (void) pipe_write.release_to_process_lifetime("pselect consumer stuck");
    /* The dup2-installed descriptors stay open as well: selected_fds_installed
   * remains set and nothing below closes them. */
}

void SelectStackRoute::destroy() noexcept {
    for (int fd = 0; fd < 3; fd++) {
        if (stdio_backup[fd].valid()) dup2(stdio_backup[fd].get(), fd);
    }
    if (consumer_stuck) {
        (void) fail(34, select_errno);
        status.code = ROUTE_DIRTY_FAILURE;
        retain_for_process_lifetime();
        return;
    }
    if (selected_fds_installed) {
        for (int fd = 3; fd < PSELECT_ROUTE_NFDS; fd++) {
            if (owned_input_set.test(fd) || owned_output_set.test(fd) ||
                owned_exception_set.test(fd)) {
                /* The fd number may also be owned by the pipe/timerfd members: drop
         * that owner before closing so nothing is closed twice. */
                if (pipe_read.get() == fd) (void) pipe_read.release();
                if (pipe_write.get() == fd) (void) pipe_write.release();
                if (block.get() == fd) (void) block.release();
                if (high_read.get() == fd) (void) high_read.release();
                close(fd);
            }
        }
        selected_fds_installed = 0;
    }
    high_read.reset();
    if (!block_borrows_pipe) block.reset();
    pipe_read.reset();
    pipe_write.reset();
    status.userspace_clean = 1;
    if (status.code != ROUTE_OK && status.kernel_disarmed) {
        status.code = ROUTE_FALLBACK_SAFE;
    }
}
