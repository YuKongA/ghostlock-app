/* LKM versioned request channel implementation (delta batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.12, 3.12.1, 3.12.2.
 * See plugin/kernel_channel.hpp for the contract. No device is touched here
 * unless LkmDeviceBinding is used; the host tests inject LkmTransport fakes. */

#include "plugin/kernel_channel.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#endif

namespace ghostlock::plugin {

    using contract::CapabilityError;
    using contract::CapabilityResult;
    using contract::CapabilityState;
    using contract::CapabilityStatus;
    using contract::MemoryChannel;

    namespace {

        [[nodiscard]] CapabilityError classify_status(std::uint32_t status) noexcept {
            if (status == static_cast<std::uint32_t>(-EPROTO)) {
                return CapabilityError::Unsupported;
            }
            return CapabilityError::Faulted;
        }

        /* The kernel may report a version mismatch as either the ioctl return
         * (transport-level -EPROTO) or as glk_lkm_req::status; both are
         * Unsupported so an ABI mismatch never degrades to a generic Faulted. */
        [[nodiscard]] CapabilityError classify_transport(int called) noexcept {
            if (called == -EPROTO) {
                return CapabilityError::Unsupported;
            }
            return CapabilityError::Faulted;
        }

    } // namespace

    LkmChannel::LkmChannel(LkmTransport transport) noexcept : transport_(transport) {}

    LkmChannel::~LkmChannel() noexcept {
        /* Drop an open device so a leaked channel never keeps the module
         * resident; the explicit release() is still the only UNLOAD path. */
        if (phase_ == LkmChannelPhase::Opened && transport_.close != nullptr) {
            transport_.close(transport_.ctx);
        }
    }

    CapabilityError LkmChannel::gate() const noexcept {
        if (phase_ == LkmChannelPhase::Unloaded) {
            return CapabilityError::Closed;
        }
        if (!established()) {
            return CapabilityError::Unavailable;
        }
        return CapabilityError::None;
    }

    CapabilityStatus LkmChannel::establish() noexcept {
        if (phase_ == LkmChannelPhase::Unloaded) {
            return std::unexpected(CapabilityError::Closed);
        }
        if (established()) {
            return {};
        }
        if (!transport_.available()) {
            return std::unexpected(CapabilityError::Unavailable);
        }
        if (transport_.open(transport_.ctx) < 0) {
            return std::unexpected(CapabilityError::Unavailable);
        }
        stats_.insmod_t =
                transport_.now != nullptr ? transport_.now(transport_.ctx) : 0U;

        glk_lkm_req req{};
        req.abi_version = GLK_LKM_ABI_VERSION;
        req.op = static_cast<std::uint32_t>(GLK_LKM_PING);
        const int called = transport_.call(transport_.ctx, req);
        if (called < 0) {
            const CapabilityError error = classify_transport(called);
            transport_.close(transport_.ctx);
            return std::unexpected(error);
        }
        if (req.status != 0U) {
            /* A version mismatch is a hard Unsupported: never guess across an
             * incompatible ABI. */
            const CapabilityError error = classify_status(req.status);
            transport_.close(transport_.ctx);
            return std::unexpected(error == CapabilityError::Unsupported
                                           ? CapabilityError::Unsupported
                                           : CapabilityError::Unavailable);
        }
        phase_ = LkmChannelPhase::Opened;
        ping_ok_ = true;
        abi_version_ = req.abi_version;
        return {};
    }

    CapabilityStatus LkmChannel::release() noexcept {
        if (phase_ == LkmChannelPhase::Unloaded) {
            return {};
        }
        if (phase_ != LkmChannelPhase::Opened) {
            return std::unexpected(CapabilityError::Unavailable);
        }
        glk_lkm_req req{};
        req.abi_version = GLK_LKM_ABI_VERSION;
        req.op = static_cast<std::uint32_t>(GLK_LKM_UNLOAD);
        const int called = transport_.call(transport_.ctx, req);
        const bool unloaded = called >= 0 && req.status == 0U;
        stats_.unload_t =
                transport_.now != nullptr ? transport_.now(transport_.ctx) : 0U;
        transport_.close(transport_.ctx);
        phase_ = LkmChannelPhase::Unloaded;
        ping_ok_ = false;
        return unloaded ? CapabilityStatus{} : std::unexpected(CapabilityError::Faulted);
    }

    CapabilityResult<std::uint64_t>
    LkmChannel::read(std::uint64_t address, std::span<std::byte> out) const noexcept {
        const CapabilityError gated = gate();
        if (gated != CapabilityError::None) {
            return std::unexpected(gated);
        }
        if (out.empty() || out.size() > static_cast<std::size_t>(GLK_LKM_MAX_XFER)) {
            return std::unexpected(CapabilityError::Rejected);
        }
        glk_lkm_req req{};
        req.abi_version = GLK_LKM_ABI_VERSION;
        req.op = static_cast<std::uint32_t>(GLK_LKM_READ);
        req.addr = address;
        req.value = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(out.data()));
        req.len = static_cast<std::uint32_t>(out.size());
        const int called = transport_.call(transport_.ctx, req);
        ++stats_.calls;
        if (called < 0) {
            return std::unexpected(classify_transport(called));
        }
        if (req.status != 0U) {
            return std::unexpected(classify_status(req.status));
        }
        return static_cast<std::uint64_t>(out.size());
    }

    CapabilityStatus
    LkmChannel::write(std::uint64_t address,
                      std::span<const std::byte> in) const noexcept {
        const CapabilityError gated = gate();
        if (gated != CapabilityError::None) {
            return std::unexpected(gated);
        }
        if (in.empty() || in.size() > static_cast<std::size_t>(GLK_LKM_MAX_XFER)) {
            return std::unexpected(CapabilityError::Rejected);
        }
        glk_lkm_req req{};
        req.abi_version = GLK_LKM_ABI_VERSION;
        req.op = static_cast<std::uint32_t>(GLK_LKM_WRITE);
        req.addr = address;
        req.value = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(in.data()));
        req.len = static_cast<std::uint32_t>(in.size());
        const int called = transport_.call(transport_.ctx, req);
        ++stats_.calls;
        if (called < 0) {
            return std::unexpected(classify_transport(called));
        }
        if (req.status != 0U) {
            return std::unexpected(classify_status(req.status));
        }
        return {};
    }

    CapabilityStatus LkmChannel::write_zero(std::uint64_t address) const noexcept {
        const CapabilityError gated = gate();
        if (gated != CapabilityError::None) {
            return std::unexpected(gated);
        }
        glk_lkm_req req{};
        req.abi_version = GLK_LKM_ABI_VERSION;
        req.op = static_cast<std::uint32_t>(GLK_LKM_WRITE_ZERO);
        req.addr = address;
        req.len = static_cast<std::uint32_t>(sizeof(std::uint64_t));
        const int called = transport_.call(transport_.ctx, req);
        ++stats_.calls;
        if (called < 0) {
            return std::unexpected(classify_transport(called));
        }
        if (req.status != 0U) {
            return std::unexpected(classify_status(req.status));
        }
        return {};
    }

    CapabilityResult<std::uint64_t>
    LkmChannel::to_direct_map(std::uint64_t image_addr) const noexcept {
        const CapabilityError gated = gate();
        if (gated != CapabilityError::None) {
            return std::unexpected(gated);
        }
        glk_lkm_req req{};
        req.abi_version = GLK_LKM_ABI_VERSION;
        req.op = static_cast<std::uint32_t>(GLK_LKM_DIRECT_MAP);
        req.value = image_addr;
        const int called = transport_.call(transport_.ctx, req);
        ++stats_.calls;
        if (called < 0) {
            return std::unexpected(classify_transport(called));
        }
        if (req.status != 0U) {
            return std::unexpected(classify_status(req.status));
        }
        return req.addr;
    }

    /* ---- LkmProxyKernelMemory ---- */

    CapabilityResult<std::uint64_t>
    LkmProxyKernelMemory::read(std::uint64_t address, std::span<std::byte> out,
                               MemoryChannel channel) noexcept {
        if (channel != MemoryChannel::LkmProxy) {
            return std::unexpected(CapabilityError::Unsupported);
        }
        if (out.empty()) {
            return std::unexpected(CapabilityError::Rejected);
        }
        std::size_t done = 0U;
        while (done < out.size()) {
            const std::size_t piece =
                    std::min<std::size_t>(out.size() - done,
                                          static_cast<std::size_t>(GLK_LKM_MAX_XFER));
            const CapabilityResult<std::uint64_t> step =
                    channel_.read(address + static_cast<std::uint64_t>(done),
                                  out.subspan(done, piece));
            if (!step.has_value()) {
                return std::unexpected(step.error());
            }
            done += piece;
        }
        return static_cast<std::uint64_t>(out.size());
    }

    CapabilityStatus
    LkmProxyKernelMemory::write(std::uint64_t address, std::span<const std::byte> in,
                                MemoryChannel channel) noexcept {
        if (channel != MemoryChannel::LkmProxy) {
            return std::unexpected(CapabilityError::Unsupported);
        }
        if (in.empty()) {
            return std::unexpected(CapabilityError::Rejected);
        }
        std::size_t done = 0U;
        while (done < in.size()) {
            const std::size_t piece =
                    std::min<std::size_t>(in.size() - done,
                                          static_cast<std::size_t>(GLK_LKM_MAX_XFER));
            const CapabilityStatus step =
                    channel_.write(address + static_cast<std::uint64_t>(done),
                                   in.subspan(done, piece));
            if (!step.has_value()) {
                return std::unexpected(step.error());
            }
            done += piece;
        }
        return {};
    }

    CapabilityStatus LkmProxyKernelMemory::write_zero(std::uint64_t address) noexcept {
        return channel_.write_zero(address);
    }

    bool LkmProxyKernelMemory::supports(MemoryChannel channel) const noexcept {
        return channel == MemoryChannel::LkmProxy && channel_.established();
    }

    bool LkmProxyKernelMemory::supports(contract::CarrierKind carrier) const noexcept {
        (void)carrier;
        return false;
    }

    CapabilityState LkmProxyKernelMemory::state(MemoryChannel channel) const noexcept {
        if (channel != MemoryChannel::LkmProxy) {
            return CapabilityState::NotSupported;
        }
        if (channel_.closed()) {
            return CapabilityState::Closed;
        }
        if (!channel_.established()) {
            return CapabilityState::NotAvailable;
        }
        return CapabilityState::Available;
    }

    CapabilityState LkmProxyKernelMemory::state(contract::CarrierKind carrier) const noexcept {
        (void)carrier;
        return CapabilityState::NotSupported;
    }

    CapabilityStatus LkmProxyKernelMemory::establish(MemoryChannel channel,
                                                     contract::CarrierKind carrier) noexcept {
        (void)carrier;
        if (channel != MemoryChannel::LkmProxy) {
            return std::unexpected(CapabilityError::Unsupported);
        }
        return channel_.establish();
    }

    CapabilityStatus LkmProxyKernelMemory::close(contract::CarrierKind carrier) noexcept {
        (void)carrier;
        return channel_.release();
    }

    /* ---- LkmProxyKernelAlias ---- */

    CapabilityResult<std::uint64_t>
    LkmProxyKernelAlias::to_direct_map(std::uint64_t image_addr) const noexcept {
        return channel_.to_direct_map(image_addr);
    }

    /* ---- LkmDeviceBinding ---- */

    LkmDeviceBinding::~LkmDeviceBinding() noexcept { close(); }

    LkmTransport LkmDeviceBinding::transport() noexcept {
        LkmTransport transport{};
        transport.ctx = static_cast<void *>(this);
        transport.open = &LkmDeviceBinding::open_thunk;
        transport.close = &LkmDeviceBinding::close_thunk;
        transport.call = &LkmDeviceBinding::call_thunk;
        transport.now = &LkmDeviceBinding::now_thunk;
        return transport;
    }

    void LkmDeviceBinding::close() noexcept {
#if defined(__linux__)
        if (fd_ >= 0) {
            (void)::close(fd_);
            fd_ = -1;
        }
#else
        fd_ = -1;
#endif
    }

#if defined(__linux__)
    int LkmDeviceBinding::open_thunk(void *ctx) noexcept {
        auto *self = static_cast<LkmDeviceBinding *>(ctx);
        if (self == nullptr) {
            return -EINVAL;
        }
        if (self->fd_ >= 0) {
            return 0;
        }
        /* O_CLOEXEC so a forked child never inherits the kernel channel. */
        self->fd_ = ::open(GLK_LKM_DEVICE_PATH, O_RDWR | O_CLOEXEC);
        return self->fd_ >= 0 ? 0 : -errno;
    }

    void LkmDeviceBinding::close_thunk(void *ctx) noexcept {
        auto *self = static_cast<LkmDeviceBinding *>(ctx);
        if (self != nullptr) {
            self->close();
        }
    }

    int LkmDeviceBinding::call_thunk(void *ctx, glk_lkm_req &req) noexcept {
        auto *self = static_cast<LkmDeviceBinding *>(ctx);
        if (self == nullptr || self->fd_ < 0) {
            return -EBADF;
        }
        if (::ioctl(self->fd_, static_cast<unsigned long>(GLK_LKM_IOCTL), &req) < 0) {
            return -errno;
        }
        return 0;
    }

    std::uint64_t LkmDeviceBinding::now_thunk(void *ctx) noexcept {
        (void)ctx;
        struct timespec ts {};
        if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            return 0U;
        }
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
               static_cast<std::uint64_t>(ts.tv_nsec);
    }
#else
    int LkmDeviceBinding::open_thunk(void *ctx) noexcept {
        (void)ctx;
        return -ENOSYS;
    }

    void LkmDeviceBinding::close_thunk(void *ctx) noexcept { (void)ctx; }

    int LkmDeviceBinding::call_thunk(void *ctx, glk_lkm_req &req) noexcept {
        (void)ctx;
        (void)req;
        return -ENOSYS;
    }

    std::uint64_t LkmDeviceBinding::now_thunk(void *ctx) noexcept {
        (void)ctx;
        return 0U;
    }
#endif

} // namespace ghostlock::plugin
