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
#include <string>

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

    /* ---- S4 logging batch B: the per-module log router ---- */

    void test_module_log_router() {
        using ghostlock::plugin::format_module_log;
        using ghostlock::plugin::kModuleLogBudgetPerRun;
        using ghostlock::plugin::kModuleLogMinIntervalNs;
        using ghostlock::plugin::make_module_ops;
        using ghostlock::plugin::module_log_accept;
        using ghostlock::plugin::ModuleLogRouter;

        /* Budget + 1 ms rate limit, driven with explicit timestamps so no test
         * ever sleeps. */
        ModuleLogRouter router{};
        router.module_id = "glk.probe";
        assert(router.budget == kModuleLogBudgetPerRun);
        router.budget = 2u; /* two messages, then the budget drops the rest */
        assert(module_log_accept(router, 1000u));
        assert(router.emitted == 1u && router.dropped == 0u);
        assert(!module_log_accept(router, 1000u + kModuleLogMinIntervalNs / 2u));
        assert(router.dropped == 1u);
        assert(module_log_accept(router, 1000u + kModuleLogMinIntervalNs));
        assert(!module_log_accept(router, 5000000u)); /* rate ok, budget spent */
        assert(router.emitted == 2u && router.dropped == 2u);

        /* Rendering: id + mapped level + sanitized single line. */
        char line[ghostlock::plugin::kModuleLogMaxBytes + 64u] = {};
        ModuleLogRouter fmt{};
        fmt.module_id = "glk.probe";
        const std::string_view ok =
                format_module_log(fmt, 2, "stage=4 read=0", line, sizeof(line));
        assert(ok == "glk.probe log(2): stage=4 read=0");

        /* Out-of-range level is clamped to 1 and marked. */
        const std::string_view clamped =
                format_module_log(fmt, 9, "x", line, sizeof(line));
        assert(clamped == "glk.probe log(1): x level_clamped=1");

        /* A newline (or any control byte) cannot forge a second line. */
        const std::string_view shapes =
                format_module_log(fmt, 0, "a\nb\tc", line, sizeof(line));
        assert(shapes == "glk.probe log(0): a_b_c");

        /* An over-long message is cut and marked, still inside the cap. */
        const std::string big(1024u, 'z');
        const std::string_view cut =
                format_module_log(fmt, 3, big, line, sizeof(line));
        assert(cut.size() <= ghostlock::plugin::kModuleLogMaxBytes);
        assert(cut.ends_with("..."));

        /* The module table keeps the caller's capabilities (forwarded) and only
         * replaces log()/ctx; child_task is carried over unchanged. */
        FakeMemory memory{};
        ghostlock::contract::Capabilities capabilities{};
        capabilities.kernel = &memory;
        HostOpsContext upstream_ctx{};
        upstream_ctx.capabilities = &capabilities;
        upstream_ctx.closed = false;
        glk_contract_ops upstream{};
        init_host_ops(upstream, upstream_ctx);
        upstream.child_task = 0x1234u;

        ModuleLogRouter module_router{};
        module_router.module_id = "glk.probe";
        const glk_contract_ops module = make_module_ops(&upstream, module_router);
        assert(module.ctx == &module_router);
        assert(module.log != upstream.log);
        assert(module.read_u64 != nullptr && module.read_u64 != upstream.read_u64);
        assert(module.child_task == 0x1234u);
        assert(module.abi_version == upstream.abi_version);
        /* Forwarding really reaches the caller's primitive. */
        std::uint64_t value = 0u;
        assert(module.read_u64(module.ctx, 0x1000u, &value) == 0);
        assert(value == memory.read_value && memory.read_calls == 1u &&
               memory.last_address == 0x1000u);

        /* A null upstream yields a log-only table (logging still works). */
        const glk_contract_ops log_only = make_module_ops(nullptr, module_router);
        assert(log_only.log != nullptr && log_only.read_u64 == nullptr);
        std::puts("plugin_host_ops_test: module_log_router OK");
    }

} // namespace

int main() {
    test_error_table();
    test_identity_and_success();
    test_error_mapping();
    test_missing_capability();
    test_closed_is_terminal();
    test_module_log_router();
    std::puts("plugin_host_ops_test: OK");
    return 0;
}
