/* Contract test for the optional address-discovery capability (ADR-0004 T3).
 * Host-only fake covers the success shape, the fail-closed normalization and
 * the handle-derived availability, so the contract is locked without any
 * device/KernelSnitch dependency. */

#include "contract/address_discovery.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using ghostlock::contract::AddressDiscoveryOps;
using ghostlock::contract::AddressDiscoveryResult;

static_assert(std::is_trivially_copyable_v<AddressDiscoveryResult>);
static_assert(std::is_standard_layout_v<AddressDiscoveryResult>);
static_assert(std::is_trivially_copyable_v<AddressDiscoveryOps>);
static_assert(std::is_standard_layout_v<AddressDiscoveryOps>);
static_assert(ghostlock::contract::AddressDiscoveryProvider<AddressDiscoveryOps>);
static_assert(noexcept(AddressDiscoveryOps{}.available()));
static_assert(noexcept(ghostlock::contract::discovery_failed()));
static_assert(ghostlock::contract::fail_closed(
    ghostlock::contract::discovery_failed()));

namespace {
    constexpr std::uintptr_t kMmStruct = 0xffff88801234a000ULL;
    constexpr std::uintptr_t kTask = 0xffff8880abcd0000ULL;

    /* Success: supplies only the field this provider owns. */
    std::int32_t fake_ok(void *, AddressDiscoveryResult *out) noexcept {
        *out = ghostlock::contract::discovery_mm_struct(kMmStruct);
        return 1;
    }

    /* Fail-closed: always the canonical all-zero failure, never a partial one. */
    std::int32_t fake_fail(void *, AddressDiscoveryResult *out) noexcept {
        *out = ghostlock::contract::discovery_failed();
        return 0;
    }
} // namespace

int32_t main(void) {
    AddressDiscoveryResult storage{};

    /* Availability is derived from the handle, not a separate flag. */
    AddressDiscoveryOps ops{};
    assert(!ops.available());
    ops.ctx = &storage;
    assert(!ops.available());
    ops.discover = fake_ok;
    assert(ops.available());
    ops.discover = nullptr;
    assert(!ops.available());

    ops.discover = fake_ok;
    AddressDiscoveryResult ok = ghostlock::contract::discovery_failed();
    assert(ops.discover(ops.ctx, &ok) == 1);
    assert(ok.ok);
    assert(ok.mm_struct == kMmStruct);
    assert(ok.kaslr_base == 0 && ok.init_task == 0 && ok.target_task == 0);
    assert(ghostlock::contract::fail_closed(ok));

    ops.discover = fake_fail;
    AddressDiscoveryResult failed{};
    failed.ok = true;
    failed.mm_struct = 0xdead;
    assert(ops.discover(ops.ctx, &failed) == 0);
    assert(!failed.ok);
    assert(failed.kaslr_base == 0 && failed.init_task == 0 &&
           failed.target_task == 0 && failed.mm_struct == 0);
    assert(ghostlock::contract::fail_closed(failed));

    /* Provider helpers never guess: a zero native value is canonical failure. */
    const AddressDiscoveryResult no_mm =
        ghostlock::contract::discovery_mm_struct(0);
    assert(!no_mm.ok && no_mm.mm_struct == 0);
    const AddressDiscoveryResult no_task =
        ghostlock::contract::discovery_target_task(0);
    assert(!no_task.ok && no_task.target_task == 0);

    /* The predicate catches a corrupted failure that kept a partial address. */
    AddressDiscoveryResult partial = ghostlock::contract::discovery_failed();
    partial.mm_struct = 0xdead;
    assert(!ghostlock::contract::fail_closed(partial));

    /* A successful result may legitimately own only one field. */
    const AddressDiscoveryResult task_only =
        ghostlock::contract::discovery_target_task(kTask);
    assert(task_only.ok && task_only.target_task == kTask &&
           task_only.mm_struct == 0);
    assert(ghostlock::contract::fail_closed(task_only));

    puts("address_discovery_test: ok");
    return 0;
}
