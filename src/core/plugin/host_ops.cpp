/* delta-4: glk_contract_ops adapter implementation. See plugin/host_ops.hpp.
 *
 * The thunks are the only place a plugin call reaches a capability. They never
 * invent a success: a missing pointer is -EOPNOTSUPP, a failed primitive keeps
 * the primitive's CapabilityError, and a closed context is -EBADF before any
 * capability is consulted. No mutable global state. */

#include "plugin/host_ops.hpp"

#include <ctime>

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

        /* ---- per-module log routing (S4 logging batch B) ----------------- */

        std::uint64_t monotonic_ns() noexcept {
            struct timespec ts {};
            if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
                return 0u; /* no clock: rate limiting degrades to budget-only */
            }
            return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
                   static_cast<std::uint64_t>(ts.tv_nsec);
        }

        void module_log_thunk(void *raw, std::int32_t level, const char *msg) noexcept {
            auto *router = static_cast<ModuleLogRouter *>(raw);
            if (router == nullptr) {
                return;
            }
            if (!module_log_accept(*router, monotonic_ns())) {
                return; /* dropped: counted by module_log_accept, never an error */
            }
            char line[kModuleLogMaxBytes + 64u] = {};
            const std::string_view text = format_module_log(
                    *router, level,
                    msg != nullptr ? std::string_view(msg) : std::string_view{}, line,
                    sizeof(line));
            if (!text.empty()) {
                std::fprintf(stderr, "[countermeasure] %.*s\n",
                             static_cast<int>(text.size()), text.data());
                std::fflush(stderr);
            }
        }

        /* Forwarding thunks: the module table keeps the caller's ctx/ops for
         * every capability operation; only log() is host-owned. A null upstream
         * operation stays null in the copy (make_module_ops only installs a
         * thunk when the caller bound one), so a plugin's own null check still
         * sees the truth. */
        template <class Fn>
        const glk_contract_ops *upstream_of(void *raw) noexcept {
            const auto *router = static_cast<const ModuleLogRouter *>(raw);
            return router != nullptr ? router->upstream : nullptr;
        }

        std::int32_t fwd_read_u64(void *raw, std::uint64_t va, std::uint64_t *out) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->read_u64(up->ctx, va, out);
        }

        std::int32_t fwd_write_u64(void *raw, std::uint64_t va, std::uint64_t value) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->write_u64(up->ctx, va, value);
        }

        std::int32_t fwd_read_bytes(void *raw, std::uint64_t va, void *dst,
                                    std::uint32_t len) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->read_bytes(up->ctx, va, dst, len);
        }

        std::int32_t fwd_write_bytes(void *raw, std::uint64_t va, const void *src,
                                     std::uint32_t len) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->write_bytes(up->ctx, va, src, len);
        }

        std::int32_t fwd_zero_word(void *raw, std::uint64_t va, const char *desc) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->zero_word(up->ctx, va, desc);
        }

        std::uint64_t fwd_image_to_direct_map(void *raw, std::uint64_t image_addr) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->image_to_direct_map(up->ctx, image_addr);
        }

        std::int32_t fwd_query_u64(void *raw, const char *path, std::uint64_t *out) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->query_u64(up->ctx, path, out);
        }

        std::int32_t fwd_query_str(void *raw, const char *path, char *buf,
                                   std::uint32_t cap) noexcept {
            const glk_contract_ops *up = upstream_of<void>(raw);
            return up->query_str(up->ctx, path, buf, cap);
        }

    } // namespace

    bool module_log_accept(ModuleLogRouter &router, std::uint64_t now_ns) noexcept {
        if (router.emitted >= router.budget) {
            ++router.dropped;
            return false;
        }
        if (router.last_ns != 0u && now_ns != 0u &&
            now_ns - router.last_ns < kModuleLogMinIntervalNs) {
            ++router.dropped;
            return false;
        }
        ++router.emitted;
        if (now_ns != 0u) {
            router.last_ns = now_ns;
        }
        return true;
    }

    std::string_view format_module_log(const ModuleLogRouter &router, std::int32_t level,
                                       std::string_view message, char *out,
                                       std::size_t capacity) noexcept {
        if (out == nullptr || capacity < 24u) {
            return {};
        }
        std::int32_t mapped = level;
        bool clamped = false;
        if (mapped < 0 || mapped > 3) {
            mapped = 1;
            clamped = true;
        }
        /* The RENDERED line (prefix included) is capped too: a plugin cannot
         * make the host print an unbounded line by sending a long message. */
        const std::size_t limit =
                capacity - 1u < kModuleLogMaxBytes ? capacity - 1u : kModuleLogMaxBytes;
        std::size_t used = 0u;
        const auto push = [&](char byte) noexcept {
            if (used + 1u <= limit) {
                out[used++] = byte;
            }
        };
        const std::string_view id = router.module_id != nullptr ? router.module_id : "?";
        for (const char byte : id) {
            push(byte);
        }
        for (const char byte : std::string_view(" log(")) {
            push(byte);
        }
        push(static_cast<char>('0' + mapped));
        for (const char byte : std::string_view("): ")) {
            push(byte);
        }
        const std::size_t reserve = clamped ? 16u : 0u; /* " level_clamped=1" */
        const std::size_t room = limit > reserve + 3u ? limit - reserve - 3u : 0u;
        bool cut = false;
        for (const char byte : message) {
            if (used >= room) {
                cut = true;
                break;
            }
            const unsigned char raw = static_cast<unsigned char>(byte);
            push(raw >= 0x20u && raw != 0x7fu ? byte : '_');
        }
        if (cut) {
            push('.');
            push('.');
            push('.');
        }
        if (clamped) {
            for (const char byte : std::string_view(" level_clamped=1")) {
                push(byte);
            }
        }
        out[used] = '\0';
        return std::string_view(out, used);
    }

    glk_contract_ops make_module_ops(const glk_contract_ops *upstream,
                                     ModuleLogRouter &router) noexcept {
        glk_contract_ops ops{};
        if (upstream != nullptr) {
            ops = *upstream;
        }
        ops.size = static_cast<std::uint32_t>(sizeof(glk_contract_ops));
        ops.abi_version = GLK_ABI_VERSION;
        router.upstream = upstream;
        ops.ctx = &router;
        ops.log = &module_log_thunk;
        ops.read_u64 =
                upstream != nullptr && upstream->read_u64 != nullptr ? &fwd_read_u64 : nullptr;
        ops.write_u64 =
                upstream != nullptr && upstream->write_u64 != nullptr ? &fwd_write_u64 : nullptr;
        ops.read_bytes = upstream != nullptr && upstream->read_bytes != nullptr
                                 ? &fwd_read_bytes
                                 : nullptr;
        ops.write_bytes = upstream != nullptr && upstream->write_bytes != nullptr
                                  ? &fwd_write_bytes
                                  : nullptr;
        ops.zero_word =
                upstream != nullptr && upstream->zero_word != nullptr ? &fwd_zero_word : nullptr;
        ops.image_to_direct_map = upstream != nullptr && upstream->image_to_direct_map != nullptr
                                          ? &fwd_image_to_direct_map
                                          : nullptr;
        ops.query_u64 =
                upstream != nullptr && upstream->query_u64 != nullptr ? &fwd_query_u64 : nullptr;
        ops.query_str =
                upstream != nullptr && upstream->query_str != nullptr ? &fwd_query_str : nullptr;
        return ops;
    }

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
