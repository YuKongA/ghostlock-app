#pragma once
/** @file contract/kernel_access.hpp
 *  The one thing a script may reach: kernel memory, by address.
 *
 *  User architecture ruling (ADR-0014, 2026-10-09): one virtual base covers every backend
 *  capability, each backend inherits it and implements the part it supports, and this object is
 *  the only surface handed to the Lua side. Lua writes numbers to addresses and reads them back;
 *  the selinux, credential and seccomp steps are not surfaces here, they are a Lua utility
 *  library that composes these primitives, so a device with working steps and a device without
 *  any differ only in the script.
 *
 *  Name: KernelAccess rather than KernelMemory. What it does is read and write at an address,
 *  through a channel carried by some mechanism; it does not own memory and is not a container.
 *
 *  Why the virtual path is allowed here: virtual dispatch enters the PI window, and the window's
 *  prohibition protects against issuing a syscall or making an indirect call *inside* it. A call
 *  made outside the window is therefore safe, and the machine check that banned vtables in the
 *  binary is being replaced by one that states that property directly. The confirmation, its
 *  reason and the technical debt are recorded in ADR-0014's conflict annex.
 *
 *  Inheritance shape: an interface with pure virtual primitives and a non-virtual convenience
 *  layer, so an implementer writes read and write and inherits the rest. `available()` is what
 *  answers "does this backend have it" -- a capability that is not implemented is a null or an
 *  `available() == false`, never a missing method.
 */
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ghostlock::contract {
    /** Which channel a read or write travels over. A backend answers which of these it has;
     *  the caller picks one explicitly rather than the implementation guessing. */
    enum class AccessChannel : std::uint8_t {
        Unavailable = 0,
        /** The PI-futex path: a write lands through the waiter window. */
        PiWindow = 1,
        /** A loaded kernel module writes on the caller's behalf. */
        KernelModule = 2,
        /** The page cache is patched and the file is the carrier. */
        PageCache = 3,
    };

    /** What the write travels in, where a channel needs one. */
    enum class AccessCarrier : std::uint8_t {
        None = 0,
        /** A prepared kernel page belonging to a sprayed object. */
        KernelPage = 1,
        /** A page-cache page of a file the caller can name. */
        FilePage = 2,
        /** The loaded module's own direct window. */
        ModuleWindow = 3,
    };

    /** How one operation ended. The codes are the tree's own: a caller acts on them without a
     *  translation table, and an unsupported channel or carrier is a refusal rather than a
     *  silent no-op. */
    struct AccessStatus final {
        enum class Code : std::uint8_t {
            Ok = 0,
            /** The address is not one this implementation may write. */
            OutOfRange = 1,
            /** The channel is not established yet. */
            NotEstablished = 2,
            /** This implementation does not have the channel or carrier. */
            Unsupported = 3,
            /** The attempt was made and failed downstream. */
            Failed = 4,
        };
        Code code = Code::Unsupported;

        [[nodiscard]] constexpr bool ok() const noexcept { return code == Code::Ok; }
    };

    [[nodiscard]] constexpr const char *access_status_name(AccessStatus status) noexcept {
        switch (status.code) {
            case AccessStatus::Code::Ok: return "ok";
            case AccessStatus::Code::OutOfRange: return "out-of-range";
            case AccessStatus::Code::NotEstablished: return "not-established";
            case AccessStatus::Code::Unsupported: return "unsupported";
            case AccessStatus::Code::Failed: return "call-failed";
        }
        return "unknown";
    }

    /** What a read produced, or why it did not. */
    struct ReadResult final {
        AccessStatus status{};
        std::uint64_t value = 0U;
    };

    /** The interface: read and write at a kernel address, through a channel, carried by
     *  something. Implementers implement the pure virtuals; read64 and write64 come for free. */
    class KernelAccess {
    public:
        KernelAccess() noexcept = default;
        KernelAccess(const KernelAccess &) = delete;
        KernelAccess &operator=(const KernelAccess &) = delete;
        virtual ~KernelAccess() noexcept = default;

        /** Read `size` bytes from an address into `out`. The caller picks the channel. */
        [[nodiscard]] virtual AccessStatus read(std::uint64_t address, std::span<std::byte> out,
                                                 AccessChannel channel) noexcept = 0;
        /** Write `in` to an address over a channel. */
        virtual AccessStatus write(std::uint64_t address, std::span<const std::byte> in,
                                   AccessChannel channel) noexcept = 0;
        /** Write one zero word. Separate because it is the bootstrap every other write needs
         *  and some implementations can do it and nothing else. */
        virtual AccessStatus write_zero(std::uint64_t address) noexcept = 0;
        /** Whether this implementation has the channel. */
        [[nodiscard]] virtual bool supports(AccessChannel channel) const noexcept = 0;
        /** Whether this implementation has the carrier. */
        [[nodiscard]] virtual bool supports(AccessCarrier carrier) const noexcept = 0;
        /** Whether anything at all is available. A backend without a write path answers false
         *  here and says so, rather than being absent from the tree. */
        [[nodiscard]] virtual bool available() const noexcept = 0;

        /** Bring a channel up over a carrier. Default: nothing to do, which is what an
         *  implementation whose channel always exists wants. */
        [[nodiscard]] virtual AccessStatus establish(AccessChannel, AccessCarrier) noexcept {
            return AccessStatus{AccessStatus::Code::Ok};
        }
        /** Take a carrier down. Default: nothing to do. */
        [[nodiscard]] virtual AccessStatus close(AccessCarrier) noexcept {
            return AccessStatus{AccessStatus::Code::Ok};
        }

        /** One word read over a channel. Not virtual: implementers write `read`. */
        [[nodiscard]] ReadResult read64(std::uint64_t address, AccessChannel channel) noexcept {
            std::byte raw[sizeof(std::uint64_t)] = {};
            const AccessStatus status = read(address, std::span<std::byte>(raw), channel);
            ReadResult result{};
            result.status = status;
            if (status.ok()) {
                std::uint64_t value = 0U;
                for (std::size_t i = 0U; i < sizeof(std::uint64_t); ++i) {
                    value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(raw[i])) << (8U * i);
                }
                result.value = value;
            }
            return result;
        }

        /** One word write over a channel. Not virtual, same reason. */
        [[nodiscard]] AccessStatus write64(std::uint64_t address, std::uint64_t value,
                                           AccessChannel channel) noexcept {
            std::byte raw[sizeof(std::uint64_t)] = {};
            for (std::size_t i = 0U; i < sizeof(std::uint64_t); ++i) {
                raw[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
            }
            return write(address, std::span<const std::byte>(raw), channel);
        }
    };
}  // namespace ghostlock::contract
