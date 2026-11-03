#include "race/pi_race.h"

using namespace ghostlock;

bool ghostlock::race::PiRace::reset(
    uint32_t initial_delay_usec, int32_t main_cpu_value, int32_t consumer_cpu_value) noexcept {
    if (waiter_owner.joinable() || owner_owner.joinable() ||
        consumer_owner.joinable()) {
        return false;
    }
    waiter_owner = support::PthreadOwner{};
    owner_owner = support::PthreadOwner{};
    consumer_owner = support::PthreadOwner{};
    wait_futex = 0;
    target_futex = 0;
    chain_futex = 0;
    waiter_ready.store(0, std::memory_order_relaxed);
    waiter_waiting.store(0, std::memory_order_relaxed);
    owner_started.store(0, std::memory_order_relaxed);
    owner_chain_done.store(0, std::memory_order_relaxed);
    owner_stop.store(0, std::memory_order_relaxed);
    route_done.store(0, std::memory_order_relaxed);
    waiter_tid.store(0, std::memory_order_relaxed);
    consumer_go.store(0, std::memory_order_relaxed);
    consumer_stop.store(0, std::memory_order_relaxed);
    consumer_calls.store(0, std::memory_order_relaxed);
    consumer_success.store(0, std::memory_order_relaxed);
    consumer_inflight.store(0, std::memory_order_relaxed);
    route_delay_usec.store(initial_delay_usec,
                           std::memory_order_relaxed);
    fast_repair.store(0, std::memory_order_relaxed);
    main_cpu = main_cpu_value;
    consumer_cpu = consumer_cpu_value;
    request = nullptr;
    route_status = route::RouteStatus{.code = route::ROUTE_RETRYABLE};
    return true;
}

int32_t ghostlock::race::PiRace::start_threads(
    void *(*waiter_entry)(void *), void *(*owner_entry)(void *),
    void *(*consumer_entry)(void *), const memory::WriteRequest * route_request)
noexcept
 {
    if (!waiter_entry || !owner_entry || !consumer_entry) return EINVAL;
    request = route_request;
    int32_t error = consumer_owner.start(consumer_entry, this);
    if (error) {
        request = nullptr;
        return error;
    }
    error = owner_owner.start(owner_entry, this);
    if (error) {
        const int32_t cleanup_error = abort_startup();
        if (cleanup_error != 0) {
            route_status = route::RouteStatus{
                .code = route::ROUTE_DIRTY_FAILURE,
                .step = 19,
                .error_number = cleanup_error,
            };
            return cleanup_error;
        }
        return error;
    }
    error = waiter_owner.start(waiter_entry, this);
    if (error) {
        const int32_t cleanup_error = abort_startup();
        if (cleanup_error != 0) {
            route_status = route::RouteStatus{
                .code = route::ROUTE_DIRTY_FAILURE,
                .step = 19,
                .error_number = cleanup_error,
            };
            return cleanup_error;
        }
        return error;
    }
    return 0;
}

int32_t ghostlock::race::PiRace::abort_startup() noexcept {
    consumer_stop.store(1);
    owner_stop.store(1);
    owner_owner.request_stop();
    consumer_owner.request_stop();
    const int32_t owner_error = owner_owner.joinable() ? owner_owner.join() : 0;
    const int32_t consumer_error = consumer_owner.joinable() ? consumer_owner.join() : 0;
    const int32_t first_error = owner_error != 0 ? owner_error : consumer_error;
    if (first_error == 0) request = nullptr;
    return first_error;
}

void ghostlock::race::PiRace::request_stop() noexcept {
    consumer_go.store(0);
    consumer_stop.store(1);
    owner_stop.store(1);
}

int32_t ghostlock::race::PiRace::join() noexcept {
    int32_t first_error = 0;
    const int32_t waiter_error = waiter_owner.joinable() ? waiter_owner.join() : 0;
    if (waiter_error != 0) first_error = waiter_error;
    const int32_t owner_error = owner_owner.joinable() ? owner_owner.join() : 0;
    if (first_error == 0 && owner_error != 0) {
        first_error = owner_error;
    }
    const int32_t consumer_error = consumer_owner.joinable() ? consumer_owner.join() : 0;
    if (first_error == 0 && consumer_error != 0) {
        first_error = consumer_error;
    }
    if (first_error == 0) request = nullptr;
    return first_error;
}

ghostlock::route::RouteStatus ghostlock::race::PiRace::outcome_with_counters(
    const route::RouteStatus &outcome, int32_t calls, int32_t success) noexcept {
    if (outcome.code == route::ROUTE_OK && (calls == 0 || success == 0)) {
        route::RouteStatus retryable = outcome;
        retryable.code = route::ROUTE_RETRYABLE;
        return retryable;
    }
    return outcome;
}
