/* delta-4 host test for plugin/host_ops.{hpp,cpp}.
 *
 * HostOpsContext adapts a caller-owned contract::Capabilities view into the C
 * glk_contract_ops table a plugin receives. The fakes below stand in for a
 * backend without a device. The assertions are:
 *   - the error table maps every CapabilityError to its documented errno;
 *   - each ABI operation reaches the matching C++ primitive and returns 0 on
 *     success (never a fabricated success);
 *   - a missing capability is -EOPNOTSUPP, a short read is -EIO, bad
 *     arguments are -EINVAL;
 *   - query_u64/query_str are -EOPNOTSUPP (no neutral query surface exists);
 *   - after close_host_ops() every int op is -EBADF and image_to_direct_map
 *     is 0 (the ABI sentinel), even with the capabilities still installed. */

#include "plugin/host_ops.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {

    using ghostlock::contract::CapabilityError;
    using ghostlock::contract::CapabilityResult;
    using ghostlock::contract::CapabilityState;
    using ghostlock::contract::CapabilityStatus;
    using ghostlock::contract::CarrierKind;
    using ghostlock::contract::MemoryChannel;
    using ghostlock::plugin::HostOpsContext;
    using ghostlock::plugin::close_host_ops;
    using ghostlock::plugin::host_ops_error_code;
    using ghostlock::plugin::init_host_ops;

    struct FakeMemory final : ghostlock::contract::KernelMemory {
        CapabilityError next_error = CapabilityError::None;
        std::uint64_t read_value = 0x1122334455667788ULL;
        /* When non-zero, read() reports this many bytes instead of the span. */
        std::uint64_t read_len_override = 0U;
        std::uint32_t read_calls = 0U;
        std::uint32_t write_calls = 0U;
        std::uint32_t zero_calls = 0U;
        std::uint64_t last_address = 0U;
        std::uint64_t last_written = 0U;
        std::uint32_t last_write_len = 0U;

        CapabilityResult<std::uint64_t>
        read(std::uint64_t address, std::span<std::byte> out,
             MemoryChannel) noexcept override {
            ++read_calls;
            last_address = address;
            if (next_error != CapabilityError::None) {
                return std::unexpected(next_error);
            }
            for (std::size_t i = 0U; i < out.size(); ++i) {
                out[i] = static_cast<std::byte>((read_value >> ((i % 8U) * 8U)) & 0xFFU);
            }
            if (read_len_override != 0U) {
                return read_len_override;
            }
            return static_cast<std::uint64_t>(out.size());
        }

        CapabilityStatus write(std::uint64_t address, std::span<const std::byte> in,
                               MemoryChannel) noexcept override {
            ++write_calls;
            last_address = address;
            last_write_len = static_cast<std::uint32_t>(in.size());
            if (!in.empty()) {
                std::uint64_t value = 0U;
                for (std::size_t i = 0U; i < in.size() && i < 8U; ++i) {
                    value |= static_cast<std::uint64_t>(
                                     static_cast<unsigned char>(in[i]))
                             << (i * 8U);
                }
                last_written = value;
            }
            if (next_error != CapabilityError::None) {
                return std::unexpected(next_error);
            }
            return {};
        }

        CapabilityStatus write_zero(std::uint64_t address) noexcept override {
            ++zero_calls;
            last_address = address;
            if (next_error != CapabilityError::None) {
                return std::unexpected(next_error);
            }
            return {};
        }

        [[nodiscard]] bool supports(MemoryChannel channel) const noexcept override {
            return channel == MemoryChannel::LkmProxy;
        }
        [[nodiscard]] bool supports(CarrierKind) const noexcept override {
            return false;
        }
    };

    struct FakeAlias final : ghostlock::contract::KernelAlias {
        CapabilityError next_error = CapabilityError::None;
        std::uint64_t mapped = 0xFFFF000000001000ULL;

        CapabilityResult<std::uint64_t>
        to_direct_map(std::uint64_t) const noexcept override {
            if (next_error != CapabilityError::None) {
                return std::unexpected(next_error);
            }
            return mapped;
        }
    };

    struct FakeChild final : ghostlock::contract::ChildTask {
        CapabilityError next_error = CapabilityError::None;
        std::uint64_t task = 0xABCDEFULL;

        CapabilityResult<std::uint64_t> current() const noexcept override {
            if (next_error != CapabilityError::None) {
                return std::unexpected(next_error);
            }
            return task;
        }
    };

    void test_error_table() {
        assert(host_ops_error_code(CapabilityError::None) ==
               ghostlock::plugin::kHostOpsOk);
        assert(host_ops_error_code(CapabilityError::Unsupported) ==
               ghostlock::plugin::kHostOpsUnsupported);
        assert(host_ops_error_code(CapabilityError::Unavailable) ==
               ghostlock::plugin::kHostOpsUnavailable);
        assert(host_ops_error_code(CapabilityError::Faulted) ==
               ghostlock::plugin::kHostOpsFaulted);
        assert(host_ops_error_code(CapabilityError::Rejected) ==
               ghostlock::plugin::kHostOpsRejected);
        assert(host_ops_error_code(CapabilityError::Closed) ==
               ghostlock::plugin::kHostOpsClosed);
        assert(ghostlock::plugin::kHostOpsUnsupported < 0);
        assert(ghostlock::plugin::kHostOpsClosed < 0);
    }

    void test_identity_and_success() {
        FakeMemory memory{};
        FakeAlias alias{};
        FakeChild child{};
        ghostlock::contract::Capabilities caps{};
        caps.kernel = &memory;
        caps.alias = &alias;
        caps.child = &child;

        HostOpsContext ctx{};
        ctx.capabilities = &caps;
        ctx.closed = false;
        glk_contract_ops ops{};
        init_host_ops(ops, ctx);

        assert(ops.size == sizeof(glk_contract_ops));
        assert(ops.abi_version == GLK_ABI_VERSION);
        assert(ops.ctx == &ctx);
        assert(ops.read_u64 != nullptr && ops.write_u64 != nullptr);
        assert(ops.read_bytes != nullptr && ops.write_bytes != nullptr);
        assert(ops.zero_word != nullptr && ops.image_to_direct_map != nullptr);
        assert(ops.query_u64 != nullptr && ops.query_str != nullptr);
        assert(ops.log != nullptr);
        assert(ops.child_task == 0xABCDEFULL);

        std::uint64_t value = 0U;
        assert(ops.read_u64(&ctx, 0x1000U, &value) == 0);
        assert(value == memory.read_value);
        assert(memory.last_address == 0x1000U);

        assert(ops.write_u64(&ctx, 0x2000U, 0xDEADBEEFCAFEF00DULL) == 0);
        assert(memory.write_calls == 1U);
        assert(memory.last_written == 0xDEADBEEFCAFEF00DULL);

        std::uint8_t bytes[4] = {};
        assert(ops.read_bytes(&ctx, 0x3000U, bytes,
                                 static_cast<std::uint32_t>(sizeof(bytes))) == 0);
        assert(memory.read_calls == 2U);

        const std::uint8_t out[4] = {1U, 2U, 3U, 4U};
        assert(ops.write_bytes(&ctx, 0x4000U, out,
                                 static_cast<std::uint32_t>(sizeof(out))) == 0);
        assert(memory.last_write_len == sizeof(out));

        assert(ops.zero_word(&ctx, 0x5000U, "clear") == 0);
        assert(memory.zero_calls == 1U);

        assert(ops.image_to_direct_map(&ctx, 0x1000U) == alias.mapped);
        assert(ops.image_to_direct_map(&ctx, 0x2000U) != 0U);

        char text[8] = {};
        assert(ops.query_u64(&ctx, "p", &value) ==
               ghostlock::plugin::kHostOpsUnsupported);
        assert(ops.query_str(&ctx, "p", text,
                              static_cast<std::uint32_t>(sizeof(text))) ==
               ghostlock::plugin::kHostOpsUnsupported);
    }

    void test_error_mapping() {
        FakeMemory memory{};
        FakeAlias alias{};
        ghostlock::contract::Capabilities caps{};
        caps.kernel = &memory;
        caps.alias = &alias;
        HostOpsContext ctx{};
        ctx.capabilities = &caps;
        ctx.closed = false;
        glk_contract_ops ops{};
        init_host_ops(ops, ctx);

        const CapabilityError errors[] = {
                CapabilityError::Unsupported,
                CapabilityError::Unavailable,
                CapabilityError::Faulted,
                CapabilityError::Rejected,
                CapabilityError::Closed,
        };
        const std::int32_t expected[] = {
                ghostlock::plugin::kHostOpsUnsupported,
                ghostlock::plugin::kHostOpsUnavailable,
                ghostlock::plugin::kHostOpsFaulted,
                ghostlock::plugin::kHostOpsRejected,
                ghostlock::plugin::kHostOpsClosed,
        };
        for (std::size_t i = 0U; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            memory.next_error = errors[i];
            std::uint64_t value = 0U;
            assert(ops.read_u64(&ctx, 0x1000U, &value) == expected[i]);
            assert(ops.write_u64(&ctx, 0x1000U, 1U) == expected[i]);
            assert(ops.zero_word(&ctx, 0x1000U, nullptr) == expected[i]);
            const std::uint8_t src[1] = {0U};
            assert(ops.write_bytes(&ctx, 0x1000U, src, 1U) == expected[i]);
        }
        memory.next_error = CapabilityError::None;

        /* A short read is a failure, never a partial success. */
        memory.read_len_override = 3U;
        std::uint8_t bytes[8] = {};
        assert(ops.read_bytes(&ctx, 0x1000U, bytes,
                            static_cast<std::uint32_t>(sizeof(bytes))) ==
               ghostlock::plugin::kHostOpsFaulted);
        memory.read_len_override = 0U;

        /* Bad arguments fail closed. */
        assert(ops.read_u64(&ctx, 0x1000U, nullptr) ==
               ghostlock::plugin::kHostOpsRejected);
        assert(ops.read_bytes(&ctx, 0x1000U, bytes, 0U) ==
               ghostlock::plugin::kHostOpsRejected);
        assert(ops.write_bytes(&ctx, 0x1000U, nullptr, 4U) ==
               ghostlock::plugin::kHostOpsRejected);
    }

    void test_missing_capability() {
        ghostlock::contract::Capabilities empty{};
        HostOpsContext ctx{};
        ctx.capabilities = &empty;
        ctx.closed = false;
        glk_contract_ops ops{};
        init_host_ops(ops, ctx);
        assert(ops.child_task == 0U);

        std::uint64_t value = 0U;
        assert(ops.read_u64(&ctx, 0x1000U, &value) ==
               ghostlock::plugin::kHostOpsUnsupported);
        assert(ops.write_u64(&ctx, 0x1000U, 1U) ==
               ghostlock::plugin::kHostOpsUnsupported);
        assert(ops.zero_word(&ctx, 0x1000U, nullptr) ==
               ghostlock::plugin::kHostOpsUnsupported);
        assert(ops.image_to_direct_map(&ctx, 0x1000U) == 0U);

        /* A null context is a rejected argument, not a crash. */
        assert(ops.read_u64(nullptr, 0x1000U, &value) ==
               ghostlock::plugin::kHostOpsRejected);
        assert(ops.image_to_direct_map(nullptr, 0x1000U) == 0U);
    }

    void test_closed_is_terminal() {
        FakeMemory memory{};
        FakeAlias alias{};
        ghostlock::contract::Capabilities caps{};
        caps.kernel = &memory;
        caps.alias = &alias;
        HostOpsContext ctx{};
        ctx.capabilities = &caps;
        ctx.closed = false;
        glk_contract_ops ops{};
        init_host_ops(ops, ctx);

        close_host_ops(ctx);
        assert(ctx.closed);
        assert(ctx.capabilities == nullptr);

        std::uint64_t value = 0U;
        const std::uint8_t src[1] = {0U};
        std::uint8_t dst[1] = {};
        assert(ops.read_u64(&ctx, 0x1000U, &value) ==
               ghostlock::plugin::kHostOpsClosed);
        assert(ops.write_u64(&ctx, 0x1000U, 1U) ==
               ghostlock::plugin::kHostOpsClosed);
        assert(ops.read_bytes(&ctx, 0x1000U, dst,
                            static_cast<std::uint32_t>(sizeof(dst))) ==
               ghostlock::plugin::kHostOpsClosed);
        assert(ops.write_bytes(&ctx, 0x1000U, src,
                             static_cast<std::uint32_t>(sizeof(src))) ==
               ghostlock::plugin::kHostOpsClosed);
        assert(ops.zero_word(&ctx, 0x1000U, nullptr) ==
               ghostlock::plugin::kHostOpsClosed);
        assert(ops.query_u64(&ctx, "p", &value) ==
               ghostlock::plugin::kHostOpsClosed);
        char text[4] = {};
        assert(ops.query_str(&ctx, "p", text, sizeof(text)) ==
               ghostlock::plugin::kHostOpsClosed);
        /* The ABI's image_to_direct_map returns uint64_t; 0 is "cannot
         * translate", which is the documented terminal answer. */
        assert(ops.image_to_direct_map(&ctx, 0x1000U) == 0U);
        assert(memory.read_calls == 0U && memory.write_calls == 0U &&
               memory.zero_calls == 0U);
    }

} // namespace

int main() {
    test_error_table();
    test_identity_and_success();
    test_error_mapping();
    test_missing_capability();
    test_closed_is_terminal();
    std::puts("plugin_host_ops_test: OK");
    return 0;
}
