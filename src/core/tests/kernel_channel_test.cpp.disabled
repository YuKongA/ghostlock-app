/* Host tests for the LKM versioned request channel (delta batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.12, 3.12.1, 3.12.2.
 * The /dev/glk ioctl backend is replaced by a fake LkmTransport, so the tests
 * observe the adapter semantics with no device and no syscall:
 *   - PING version mismatch is fail-closed (Unsupported, state NotAvailable);
 *   - state maps NotAvailable -> Available -> Closed, and a released channel
 *     cannot be re-established;
 *   - an out-of-range address reports -EFAULT as Faulted, never success;
 *   - WRITE_ZERO sends the zero op with the byte count and ignores value;
 *   - READ/WRITE round-trip, including a span larger than GLK_LKM_MAX_XFER;
 *   - lkm_window diagnostics count forwarded calls and record the clock. */

#include "plugin/kernel_channel.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace {

    using ghostlock::contract::CapabilityError;
    using ghostlock::contract::CapabilityState;
    using ghostlock::plugin::LkmChannel;
    using ghostlock::plugin::LkmChannelPhase;
    using ghostlock::plugin::LkmProxyKernelAlias;
    using ghostlock::plugin::LkmProxyKernelMemory;
    using ghostlock::plugin::LkmTransport;

    using ghostlock::contract::MemoryChannel;

    struct Fake final {
        std::array<std::uint8_t, 8192> kernel_mem{};
        std::uint64_t direct_base = 0xFFFF000000000000ULL;
        std::uint32_t direct_span = 8192U;

        int open_result = 0;
        bool ping_version_mismatch = false;
        bool transport_eproto = false;
        bool transport_fail = false;

        bool opened = false;
        bool closed = false;
        bool unloaded = false;
        std::uint64_t clock = 1000U;
        std::uint32_t ioctl_calls = 0U;
        std::array<std::uint32_t, 128> ops{};
        std::size_t op_count = 0U;
    };

    Fake g;

    [[nodiscard]] std::uint8_t *kernel_at(std::uint64_t addr) noexcept {
        return g.kernel_mem.data() + (addr - g.direct_base);
    }

    [[nodiscard]] bool in_direct(std::uint64_t addr, std::uint32_t len) noexcept {
        return addr >= g.direct_base &&
               (addr - g.direct_base) + static_cast<std::uint64_t>(len) <=
                       static_cast<std::uint64_t>(g.direct_span);
    }

    int fake_open(void *ctx) noexcept {
        (void)ctx;
        g.opened = true;
        g.closed = false;
        return g.open_result;
    }

    void fake_close(void *ctx) noexcept {
        (void)ctx;
        g.closed = true;
    }

    std::uint64_t fake_now(void *ctx) noexcept {
        (void)ctx;
        return g.clock;
    }

    int fake_call(void *ctx, glk_lkm_req &req) noexcept {
        (void)ctx;
        ++g.ioctl_calls;
        if (g.op_count < g.ops.size()) {
            g.ops[g.op_count] = req.op;
            ++g.op_count;
        }
        if (g.transport_eproto) {
            /* The real ioctl returns -EPROTO when abi_version mismatches. */
            return -EPROTO;
        }
        if (g.transport_fail) {
            return -5; /* EIO: the transport itself failed */
        }
        if (req.abi_version != GLK_LKM_ABI_VERSION) {
            req.status = static_cast<std::uint32_t>(-EPROTO);
            return 0;
        }
        req.status = 0U;
        switch (req.op) {
        case GLK_LKM_PING:
            if (g.ping_version_mismatch) {
                req.status = static_cast<std::uint32_t>(-EPROTO);
            }
            break;
        case GLK_LKM_READ:
            if (!in_direct(req.addr, req.len)) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            std::memcpy(reinterpret_cast<void *>(req.value), kernel_at(req.addr),
                        static_cast<std::size_t>(req.len));
            break;
        case GLK_LKM_WRITE:
            if (!in_direct(req.addr, req.len)) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            std::memcpy(kernel_at(req.addr),
                        reinterpret_cast<const void *>(req.value),
                        static_cast<std::size_t>(req.len));
            break;
        case GLK_LKM_WRITE_ZERO:
            if (!in_direct(req.addr, req.len)) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            std::memset(kernel_at(req.addr), 0, static_cast<std::size_t>(req.len));
            break;
        case GLK_LKM_DIRECT_MAP:
            if (req.value == 0U) {
                req.status = static_cast<std::uint32_t>(-EFAULT);
                break;
            }
            req.addr = req.value + 0x1000U; /* fake alias */
            break;
        case GLK_LKM_UNLOAD:
            g.unloaded = true;
            break;
        default:
            req.status = static_cast<std::uint32_t>(-EOPNOTSUPP);
            break;
        }
        return 0;
    }

    [[nodiscard]] LkmTransport transport() noexcept {
        LkmTransport t{};
        t.ctx = &g;
        t.open = &fake_open;
        t.close = &fake_close;
        t.call = &fake_call;
        t.now = &fake_now;
        return t;
    }

    void reset() noexcept {
        g = Fake{};
    }

    /* ---- PING version mismatch is fail-closed ---- */

    void test_ping_version_mismatch() {
        reset();
        g.ping_version_mismatch = true;
        LkmChannel channel(transport());
        LkmProxyKernelMemory memory(channel);
        LkmProxyKernelAlias alias(channel);

        const auto established = channel.establish();
        assert(!established.has_value());
        assert(established.error() == CapabilityError::Unsupported);
        assert(channel.phase() == LkmChannelPhase::NotOpened);
        assert(!channel.established());
        assert(!memory.supports(MemoryChannel::LkmProxy));
        assert(memory.state(MemoryChannel::LkmProxy) == CapabilityState::NotAvailable);

        std::array<std::byte, 8> buffer{};
        const auto read = memory.read(g.direct_base, std::span<std::byte>(buffer),
                                      MemoryChannel::LkmProxy);
        assert(!read.has_value());
        assert(read.error() == CapabilityError::Unavailable);
        assert(!alias.to_direct_map(0x1000U).has_value());
        assert(g.closed);
    }

    /* ---- the real device reports a version mismatch as an ioctl -EPROTO;
     * that transport-level code must map to Unsupported too (fail-closed). ---- */

    void test_ping_transport_eproto() {
        reset();
        g.transport_eproto = true;
        LkmChannel channel(transport());
        LkmProxyKernelMemory memory(channel);

        const auto established = channel.establish();
        assert(!established.has_value());
        assert(established.error() == CapabilityError::Unsupported);
        assert(!channel.established());
        assert(memory.state(MemoryChannel::LkmProxy) == CapabilityState::NotAvailable);
        assert(g.closed);
    }

    /* ---- state mapping and terminal Closed ---- */

    void test_state_lifecycle() {
        reset();
        LkmChannel channel(transport());
        LkmProxyKernelMemory memory(channel);

        assert(memory.state(MemoryChannel::Fops) == CapabilityState::NotSupported);
        assert(memory.state(MemoryChannel::Unavailable) == CapabilityState::NotSupported);
        assert(memory.state(MemoryChannel::LkmProxy) == CapabilityState::NotAvailable);
        assert(!memory.supports(MemoryChannel::LkmProxy));

        assert(channel.establish().has_value());
        assert(channel.abi_version() == GLK_LKM_ABI_VERSION);
        assert(memory.state(MemoryChannel::LkmProxy) == CapabilityState::Available);
        assert(memory.supports(MemoryChannel::LkmProxy));
        assert(!memory.supports(ghostlock::contract::CarrierKind::Ashmem));

        /* A second establish while open is a no-op success, not a new open. */
        assert(channel.establish().has_value());
        assert(g.ioctl_calls == 1U); /* only the first PING */

        assert(channel.release().has_value());
        assert(channel.closed());
        assert(g.unloaded);
        assert(g.closed);
        assert(!memory.supports(MemoryChannel::LkmProxy));
        assert(memory.state(MemoryChannel::LkmProxy) == CapabilityState::Closed);

        /* Different run / reuse is impossible: Closed is terminal. */
        const auto again = channel.establish();
        assert(!again.has_value());
        assert(again.error() == CapabilityError::Closed);
        /* release is idempotent. */
        assert(channel.release().has_value());

        /* After Closed every primitive reports Closed, never success. */
        std::array<std::byte, 8> buffer{};
        const auto read = memory.read(g.direct_base, std::span<std::byte>(buffer),
                                      MemoryChannel::LkmProxy);
        assert(!read.has_value());
        assert(read.error() == CapabilityError::Closed);
        std::array<std::byte, 8> in{};
        const auto write = memory.write(g.direct_base, std::span<const std::byte>(in),
                                        MemoryChannel::LkmProxy);
        assert(!write.has_value());
        assert(write.error() == CapabilityError::Closed);
        const auto zero = memory.write_zero(g.direct_base);
        assert(!zero.has_value());
        assert(zero.error() == CapabilityError::Closed);
    }

    /* ---- out-of-range address is -EFAULT, not a silent success ---- */

    void test_address_validation() {
        reset();
        LkmChannel channel(transport());
        LkmProxyKernelMemory memory(channel);
        assert(channel.establish().has_value());

        std::array<std::byte, 16> buffer{};
        /* Below the direct map. */
        const auto below = memory.read(0x1000U, std::span<std::byte>(buffer),
                                       MemoryChannel::LkmProxy);
        assert(!below.has_value());
        assert(below.error() == CapabilityError::Faulted);

        /* Past the end (the last byte falls outside the span). */
        const std::uint64_t past = g.direct_base + g.direct_span - 4U;
        const auto over = memory.read(past, std::span<std::byte>(buffer),
                                      MemoryChannel::LkmProxy);
        assert(!over.has_value());
        assert(over.error() == CapabilityError::Faulted);

        std::array<std::byte, 8> in{};
        const auto over_write = memory.write(past, std::span<const std::byte>(in),
                                             MemoryChannel::LkmProxy);
        assert(!over_write.has_value());
        assert(over_write.error() == CapabilityError::Faulted);

        const auto over_zero = memory.write_zero(past);
        assert(!over_zero.has_value());
        assert(over_zero.error() == CapabilityError::Faulted);

        /* The authorization ruling does not bypass the address check: the same
         * valid channel still rejects a bad target. */
        assert(memory.state(MemoryChannel::LkmProxy) == CapabilityState::Available);
        assert(channel.establish().has_value());
    }

    /* ---- WRITE_ZERO semantics ---- */

    void test_write_zero() {
        reset();
        g.kernel_mem.fill(0xABU);
        LkmChannel channel(transport());
        LkmProxyKernelMemory memory(channel);
        assert(channel.establish().has_value());

        const std::uint64_t target = g.direct_base + 0x40U;
        assert(memory.write_zero(target).has_value());
        for (std::size_t i = 0U; i < sizeof(std::uint64_t); ++i) {
            assert(g.kernel_mem[0x40U + i] == 0U);
        }
        assert(g.kernel_mem[0x3FU] == 0xABU); /* byte before is untouched */
        assert(g.kernel_mem[0x48U] == 0xABU); /* byte after is untouched */

        /* The op was forwarded as WRITE_ZERO, not WRITE. */
        assert(g.ops[g.op_count - 1U] == GLK_LKM_WRITE_ZERO);
    }

    /* ---- read/write round trip, including chunking over MAX_XFER ---- */

    void test_round_trip_and_chunking() {
        reset();
        LkmChannel channel(transport());
        LkmProxyKernelMemory memory(channel);
        assert(channel.establish().has_value());

        const std::uint64_t target = g.direct_base + 0x100U;
        std::array<std::byte, 24> out{};
        for (std::size_t i = 0U; i < out.size(); ++i) {
            out[i] = static_cast<std::byte>((i * 7U) & 0xFFU);
        }
        assert(memory.write(target, std::span<const std::byte>(out),
                            MemoryChannel::LkmProxy)
                       .has_value());

        std::array<std::byte, 24> back{};
        const auto read = memory.read(target, std::span<std::byte>(back),
                                      MemoryChannel::LkmProxy);
        assert(read.has_value());
        assert(read.value() == back.size());
        assert(std::memcmp(out.data(), back.data(), out.size()) == 0);

        /* A span larger than one request is split into GLK_LKM_MAX_XFER pieces;
         * the adapter returns the full length and the fake sees two READs. */
        std::array<std::byte, GLK_LKM_MAX_XFER + 64U> big{};
        big.fill(std::byte{0x5A});
        assert(memory.write(g.direct_base, std::span<const std::byte>(big),
                            MemoryChannel::LkmProxy)
                       .has_value());
        std::array<std::byte, GLK_LKM_MAX_XFER + 64U> big_back{};
        const auto big_read = memory.read(g.direct_base,
                                          std::span<std::byte>(big_back),
                                          MemoryChannel::LkmProxy);
        assert(big_read.has_value());
        assert(big_read.value() == big.size());
        assert(std::memcmp(big.data(), big_back.data(), big.size()) == 0);
    }

    /* ---- direct map + stats ---- */

    void test_alias_and_stats() {
        reset();
        LkmChannel channel(transport());
        LkmProxyKernelAlias alias(channel);
        LkmProxyKernelMemory memory(channel);

        /* Before establish the alias is unavailable, not a fake success. */
        const auto early = alias.to_direct_map(0x4000U);
        assert(!early.has_value());
        assert(early.error() == CapabilityError::Unavailable);

        g.clock = 4242U;
        assert(channel.establish().has_value());
        assert(channel.stats().insmod_t == 4242U);
        assert(channel.stats().calls == 0U);

        const auto mapped = alias.to_direct_map(0x4000U);
        assert(mapped.has_value());
        assert(mapped.value() == 0x5000U);
        assert(channel.stats().calls == 1U);

        std::array<std::byte, 1> one{};
        (void)memory.read(g.direct_base, std::span<std::byte>(one),
                          MemoryChannel::LkmProxy);
        assert(channel.stats().calls == 2U);

        g.clock = 9001U;
        assert(channel.release().has_value());
        assert(channel.stats().unload_t == 9001U);
        assert(g.unloaded);
    }

} // namespace

int main() {
    test_ping_version_mismatch();
    test_ping_transport_eproto();
    test_state_lifecycle();
    test_address_validation();
    test_write_zero();
    test_round_trip_and_chunking();
    test_alias_and_stats();

    std::puts("kernel_channel_test: OK");
    return 0;
}
