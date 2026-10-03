/* Contract test for capability handles (ADR-0004 T1): trivially copyable,
 * standard-layout, noexcept, and available() derived from the handle only. */

#include "contract/capabilities.hpp"

#include <cassert>
#include <cstdio>
#include <type_traits>

using ghostlock::contract::FileCacheWriteOps;
using ghostlock::contract::KernelMemoryOps;

static_assert(std::is_trivially_copyable_v<KernelMemoryOps>);
static_assert(std::is_standard_layout_v<KernelMemoryOps>);
static_assert(std::is_trivially_copyable_v<FileCacheWriteOps>);
static_assert(std::is_standard_layout_v<FileCacheWriteOps>);
static_assert(noexcept(KernelMemoryOps{}.available()));
static_assert(noexcept(FileCacheWriteOps{}.available()));

namespace {
    std::int32_t fake_read(void *, std::uint64_t, void *, std::size_t) noexcept { return 0; }
    std::int32_t fake_write(void *, std::uint64_t, const void *, std::size_t) noexcept { return 0; }
    std::int32_t fake_write16(void *, std::uint64_t, const void *) noexcept { return 0; }
} // namespace

int32_t main(void) {
    KernelMemoryOps km{};
    assert(!km.available());
    km.read = fake_read;
    assert(!km.available());
    km.write = fake_write;
    assert(km.available());
    km.read = nullptr;
    assert(!km.available());

    FileCacheWriteOps fc{};
    assert(!fc.available());
    fc.write16 = fake_write16;
    assert(fc.available());
    fc.write16 = nullptr;
    assert(!fc.available());

    puts("contract_capabilities_test: ok");
    return 0;
}
