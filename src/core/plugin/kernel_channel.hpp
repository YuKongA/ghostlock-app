#ifndef GHOSTLOCK_PLUGIN_KERNEL_CHANNEL_HPP
#define GHOSTLOCK_PLUGIN_KERNEL_CHANNEL_HPP

/* LKM versioned request channel (delta batch).
 *
 * Authority: docs/analysis/contract-design.md sections 3.12, 3.12.1, 3.12.2
 * and 4.1. The out-of-tree ghostlock.ko is the kernel-side capability provider
 * for the 43284 path. This unit is the userspace adapter: it owns the
 * /dev/glk request channel, PINGs the ABI version on establish, and exposes the
 * channel through the neutral contract interfaces KernelMemory and KernelAlias.
 *
 * There is no authorization gate by ruling; safety is the residency window plus
 * "unloaded means Closed". The channel is a mechanism, not a capability:
 * LkmProxyKernelMemory answers only MemoryChannel::LkmProxy and reports
 * Unsupported for every other mechanism, and it never turns a failure into a
 * success (R7). PING mismatch is Unsupported (fail-closed).
 *
 * The device boundary is injected as LkmTransport so host tests drive a fake
 * ioctl backend with no device. LkmDeviceBinding below is the production
 * binding to GLK_LKM_DEVICE_PATH.
 *
 * R1: plugin -> {contract, memory, support}; this header includes only the
 * standard library and contract headers.
 */

#include "contract/abi/glk_contract_abi.h"
#include "contract/capability.hpp"
#include "contract/kernel_alias.hpp"
#include "contract/kernel_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ghostlock::plugin {

    /* The only side-effecting surface. A transport that is not fully bound is
     * unavailable and establish() fails closed instead of calling through a
     * null pointer.
     *
     *   open   opens the device; 0 on success or a negative errno.
     *   close  drops the device; never fails.
     *   call   performs one ioctl. It returns 0 when the request completed (the
     *          kernel result is in glk_lkm_req::status) or a negative errno when
     *          the transport itself failed.
     *   now    optional monotonic clock for the lkm_window diagnostic. A null
     *          clock reports 0, which is enough for host assertions. */
    struct LkmTransport final {
        void *ctx = nullptr;
        int (*open)(void *ctx) noexcept = nullptr;
        void (*close)(void *ctx) noexcept = nullptr;
        int (*call)(void *ctx, glk_lkm_req &req) noexcept = nullptr;
        std::uint64_t (*now)(void *ctx) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return open != nullptr && close != nullptr && call != nullptr;
        }
    };

    /* lkm_window{insmod_t, unload_t, calls=n}: insmod_t/unload_t are the clock
     * values seen at establish/release (0 when no clock was bound) and calls is
     * the number of primitives forwarded through the channel. Recorded for the
     * device gate; no real clock is required by the host tests. */
    struct LkmWindowStats final {
        std::uint64_t insmod_t = 0U;
        std::uint64_t unload_t = 0U;
        std::uint32_t calls = 0U;
    };

    /* Lifecycle of the raw channel. NotOpened -> Opened -> Unloaded. Unloaded is
     * the terminal state: a run may not re-establish the same channel. */
    enum class LkmChannelPhase : std::uint8_t {
        NotOpened = 0U,
        Opened = 1U,
        Unloaded = 2U,
    };

    class LkmChannel final {
    public:
        LkmChannel() noexcept = default;
        explicit LkmChannel(LkmTransport transport) noexcept;
        ~LkmChannel() noexcept;
        LkmChannel(const LkmChannel &) = delete;
        LkmChannel &operator=(const LkmChannel &) = delete;

        /* Opens the device and PINGs GLK_LKM_ABI_VERSION. A version mismatch
         * returns Unsupported (fail-closed, no guess). Idempotent while open. A
         * channel that was released cannot be re-established (returns Closed). */
        [[nodiscard]] contract::CapabilityStatus establish() noexcept;

        /* Sends UNLOAD, drops the device and transitions to the Unloaded
         * terminal state. Idempotent. A failed UNLOAD is reported as Faulted so
         * a resident module is never treated as clean (fail-closed). */
        [[nodiscard]] contract::CapabilityStatus release() noexcept;

        [[nodiscard]] bool established() const noexcept {
            return phase_ == LkmChannelPhase::Opened && ping_ok_;
        }
        [[nodiscard]] bool closed() const noexcept {
            return phase_ == LkmChannelPhase::Unloaded;
        }
        [[nodiscard]] LkmChannelPhase phase() const noexcept { return phase_; }
        [[nodiscard]] std::uint32_t abi_version() const noexcept { return abi_version_; }
        [[nodiscard]] const LkmWindowStats &stats() const noexcept { return stats_; }

        /* Raw primitives: callers must gate on state first; these report
         * Unavailable/Closed directly when called out of window. */
        [[nodiscard]] contract::CapabilityResult<std::uint64_t>
        read(std::uint64_t address, std::span<std::byte> out) const noexcept;
        [[nodiscard]] contract::CapabilityStatus
        write(std::uint64_t address, std::span<const std::byte> in) const noexcept;
        [[nodiscard]] contract::CapabilityStatus write_zero(std::uint64_t address) const noexcept;
        [[nodiscard]] contract::CapabilityResult<std::uint64_t>
        to_direct_map(std::uint64_t image_addr) const noexcept;

    private:
        [[nodiscard]] contract::CapabilityError gate() const noexcept;
        [[nodiscard]] contract::CapabilityStatus submit(glk_lkm_req &req) const noexcept;

        LkmTransport transport_{};
        mutable LkmWindowStats stats_{};
        LkmChannelPhase phase_ = LkmChannelPhase::NotOpened;
        bool ping_ok_ = false;
        std::uint32_t abi_version_ = 0U;
    };

    /* contract::KernelMemory over the channel. Holds the channel; the caller
     * owns the channel and must outlive this adapter. Arbitrary spans are
     * chunked into GLK_LKM_MAX_XFER requests. */
    class LkmProxyKernelMemory final : public contract::KernelMemory {
    public:
        explicit LkmProxyKernelMemory(LkmChannel &channel) noexcept : channel_(channel) {}

        [[nodiscard]] contract::CapabilityResult<std::uint64_t>
        read(std::uint64_t address, std::span<std::byte> out,
             contract::MemoryChannel channel) noexcept override;
        contract::CapabilityStatus
        write(std::uint64_t address, std::span<const std::byte> in,
              contract::MemoryChannel channel) noexcept override;
        contract::CapabilityStatus write_zero(std::uint64_t address) noexcept override;

        [[nodiscard]] bool supports(contract::MemoryChannel channel) const noexcept override;
        [[nodiscard]] bool supports(contract::CarrierKind carrier) const noexcept override;
        [[nodiscard]] contract::CapabilityState state(
                contract::MemoryChannel channel) const noexcept override;
        [[nodiscard]] contract::CapabilityState state(
                contract::CarrierKind carrier) const noexcept override;
        contract::CapabilityStatus establish(contract::MemoryChannel channel,
                                             contract::CarrierKind carrier) noexcept override;
        contract::CapabilityStatus close(contract::CarrierKind carrier) noexcept override;

    private:
        LkmChannel &channel_;
    };

    /* contract::KernelAlias over the channel (DIRECT_MAP). */
    class LkmProxyKernelAlias final : public contract::KernelAlias {
    public:
        explicit LkmProxyKernelAlias(LkmChannel &channel) noexcept : channel_(channel) {}

        [[nodiscard]] contract::CapabilityResult<std::uint64_t>
        to_direct_map(std::uint64_t image_addr) const noexcept override;

    private:
        LkmChannel &channel_;
    };

    /* Production binding to /dev/glk. Owns the fd; transport() returns an
     * unavailable transport until open() succeeds. On a non-Linux host the
     * thunks report -ENOSYS so the host test links without a device. */
    class LkmDeviceBinding final {
    public:
        LkmDeviceBinding() noexcept = default;
        ~LkmDeviceBinding() noexcept;
        LkmDeviceBinding(const LkmDeviceBinding &) = delete;
        LkmDeviceBinding &operator=(const LkmDeviceBinding &) = delete;

        [[nodiscard]] LkmTransport transport() noexcept;
        void close() noexcept;

    private:
        static int open_thunk(void *ctx) noexcept;
        static void close_thunk(void *ctx) noexcept;
        static int call_thunk(void *ctx, glk_lkm_req &req) noexcept;
        static std::uint64_t now_thunk(void *ctx) noexcept;

        int fd_ = -1;
    };

} // namespace ghostlock::plugin

#endif
