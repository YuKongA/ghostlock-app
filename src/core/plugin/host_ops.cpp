/* delta-4: glk_contract_ops adapter implementation. See plugin/host_ops.hpp.
 *
 * The thunks are the only place a plugin call reaches a capability. They never
 * invent a success: a missing pointer is -EOPNOTSUPP, a failed primitive keeps
 * the primitive's CapabilityError, and a closed context is -EBADF before any
 * capability is consulted. No mutable global state. */

#include "plugin/host_ops.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace ghostlock::plugin {
    namespace {

        using contract::CapabilityError;
        using contract::CapabilityResult;
        using contract::CapabilityStatus;

        [[nodiscard]] HostOpsContext *context_of(void *raw) noexcept {
            return static_cast<HostOpsContext *>(raw);
        }

        /* Returns the mapped error when the context is closed, the kernel
         * capability is absent, or the primitive failed; kHostOpsOk on success.
         * The caller passes the primitive's result so the two checks stay in one
         * place. */
        [[nodiscard]] std::int32_t map_status(const CapabilityStatus &status) noexcept {
            if (!status.has_value()) {
                return host_ops_error_code(status.error());
            }
            return kHostOpsOk;
        }

        [[nodiscard]] std::int32_t map_error(CapabilityError error) noexcept {
            return host_ops_error_code(error);
        }

        /* Common prologue: null context / closed / missing KernelMemory. Returns
         * kHostOpsOk when the caller may proceed, otherwise the error code. */
        [[nodiscard]] std::int32_t gate_kernel(void *raw, HostOpsContext *&out) noexcept {
            HostOpsContext *ctx = context_of(raw);
            if (ctx == nullptr) {
                return kHostOpsRejected;
            }
            if (ctx->closed) {
                return kHostOpsClosed;
            }
            if (ctx->capabilities == nullptr || ctx->capabilities->kernel == nullptr) {
                return kHostOpsUnsupported;
            }
            out = ctx;
            return kHostOpsOk;
        }

        std::int32_t read_u64_thunk(void *raw, std::uint64_t va,
                                    std::uint64_t *out) noexcept {
            HostOpsContext *ctx = nullptr;
            const std::int32_t gate = gate_kernel(raw, ctx);
            if (gate != kHostOpsOk) {
                return gate;
            }
            if (out == nullptr) {
                return kHostOpsRejected;
            }
            const CapabilityResult<std::uint64_t> result =
                    ctx->capabilities->kernel->read64(va, ctx->channel);
            if (!result.has_value()) {
                return map_error(result.error());
            }
            *out = result.value();
            return kHostOpsOk;
        }

        std::int32_t write_u64_thunk(void *raw, std::uint64_t va,
                                     std::uint64_t value) noexcept {
            HostOpsContext *ctx = nullptr;
            const std::int32_t gate = gate_kernel(raw, ctx);
            if (gate != kHostOpsOk) {
                return gate;
            }
            return map_status(
                    ctx->capabilities->kernel->write64(va, value, ctx->channel));
        }

        std::int32_t read_bytes_thunk(void *raw, std::uint64_t va, void *dst,
                                      std::uint32_t len) noexcept {
            HostOpsContext *ctx = nullptr;
            const std::int32_t gate = gate_kernel(raw, ctx);
            if (gate != kHostOpsOk) {
                return gate;
            }
            /* len == 0 is a rejected argument, not a successful no-op: the ABI
             * carries a byte count of 1..GLK_LKM_MAX_XFER. */
            if (dst == nullptr || len == 0U) {
                return kHostOpsRejected;
            }
            const CapabilityResult<std::uint64_t> result =
                    ctx->capabilities->kernel->read(
                            va,
                            std::span<std::byte>(static_cast<std::byte *>(dst),
                                                 static_cast<std::size_t>(len)),
                            ctx->channel);
            if (!result.has_value()) {
                return map_error(result.error());
            }
            /* A short read is a failure, never a partial success (R7). */
            if (result.value() != static_cast<std::uint64_t>(len)) {
                return kHostOpsFaulted;
            }
            return kHostOpsOk;
        }

        std::int32_t write_bytes_thunk(void *raw, std::uint64_t va,
                                       const void *src, std::uint32_t len) noexcept {
            HostOpsContext *ctx = nullptr;
            const std::int32_t gate = gate_kernel(raw, ctx);
            if (gate != kHostOpsOk) {
                return gate;
            }
            if (src == nullptr || len == 0U) {
                return kHostOpsRejected;
            }
            return map_status(ctx->capabilities->kernel->write(
                    va,
                    std::span<const std::byte>(static_cast<const std::byte *>(src),
                                               static_cast<std::size_t>(len)),
                    ctx->channel));
        }

        std::int32_t zero_word_thunk(void *raw, std::uint64_t va,
                                     const char *desc) noexcept {
            (void)desc; /* diagnostics only; the primitive is a fixed zero write */
            HostOpsContext *ctx = nullptr;
            const std::int32_t gate = gate_kernel(raw, ctx);
            if (gate != kHostOpsOk) {
                return gate;
            }
            return map_status(ctx->capabilities->kernel->write_zero(va));
        }

        std::uint64_t image_to_direct_map_thunk(void *raw,
                                                std::uint64_t image_addr) noexcept {
            HostOpsContext *ctx = context_of(raw);
            if (ctx == nullptr || ctx->closed) {
                return 0U; /* the ABI sentinel: translation unavailable */
            }
            if (ctx->capabilities == nullptr || ctx->capabilities->alias == nullptr) {
                return 0U;
            }
            const CapabilityResult<std::uint64_t> result =
                    ctx->capabilities->alias->to_direct_map(image_addr);
            if (!result.has_value()) {
                return 0U;
            }
            return result.value();
        }

        /* No neutral query surface exists yet. Returning a value would be a
         * fabricated success, so the honest answer is -EOPNOTSUPP; a closed
         * context still wins with -EBADF. */
        std::int32_t query_u64_thunk(void *raw, const char *path,
                                     std::uint64_t *out) noexcept {
            (void)path;
            (void)out;
            const HostOpsContext *ctx = context_of(raw);
            if (ctx == nullptr) {
                return kHostOpsRejected;
            }
            if (ctx->closed) {
                return kHostOpsClosed;
            }
            return kHostOpsUnsupported;
        }

        std::int32_t query_str_thunk(void *raw, const char *path, char *buf,
                                     std::uint32_t cap) noexcept {
            (void)path;
            (void)buf;
            (void)cap;
            const HostOpsContext *ctx = context_of(raw);
            if (ctx == nullptr) {
                return kHostOpsRejected;
            }
            if (ctx->closed) {
                return kHostOpsClosed;
            }
            return kHostOpsUnsupported;
        }

        /* Plugin diagnostics use the plugin layer's existing stderr sink (the
         * same one plugin/loader.cpp uses). The message is never interpreted. */
        void log_thunk(void *raw, std::int32_t level, const char *msg) noexcept {
            (void)raw;
            std::fprintf(stderr, "[countermeasure] plugin log(%d): %s\n",
                         static_cast<int>(level), msg != nullptr ? msg : "(null)");
        }

    } // namespace

    void init_host_ops(glk_contract_ops &ops, HostOpsContext &ctx) noexcept {
        ops.size = static_cast<std::uint32_t>(sizeof(glk_contract_ops));
        ops.abi_version = GLK_ABI_VERSION;
        ops.ctx = &ctx;
        ops.read_u64 = &read_u64_thunk;
        ops.write_u64 = &write_u64_thunk;
        ops.read_bytes = &read_bytes_thunk;
        ops.write_bytes = &write_bytes_thunk;
        ops.zero_word = &zero_word_thunk;
        ops.image_to_direct_map = &image_to_direct_map_thunk;
        ops.query_u64 = &query_u64_thunk;
        ops.query_str = &query_str_thunk;
        ops.log = &log_thunk;
        ops.child_task = 0U;
        if (!ctx.closed && ctx.capabilities != nullptr &&
            ctx.capabilities->child != nullptr) {
            const CapabilityResult<std::uint64_t> result =
                    ctx.capabilities->child->current();
            if (result.has_value()) {
                ops.child_task = result.value();
            }
        }
    }

    void close_host_ops(HostOpsContext &ctx) noexcept {
        ctx.closed = true;
        ctx.capabilities = nullptr;
    }

} // namespace ghostlock::plugin
