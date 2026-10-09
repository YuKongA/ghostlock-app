#pragma once
/** @file contract/kernel_access.hpp
 *  The one thing a script may reach: kernel memory, by address.
 *
 *  User architecture ruling (ADR-0014, 2026-10-09), refined the same day: one virtual base
 *  covers every backend capability, each backend inherits it and implements the part it
 *  supports, and this object -- and nothing else -- is handed to the Lua side. Lua writes
 *  numbers to addresses; the selinux, credential and seccomp steps are a Lua utility library
 *  built on top, so a device with working steps and a device without differ only in the script.
 *
 *  Five refinements from the user, all in this file:
 *
 *  1. A capability is not a boolean. `state()` reports where it is: not supported, declared but
 *     not initialized, initializing, ready, or failed. A backend can offer more as the run
 *     progresses -- 43499, for instance, can write early and can only read arbitrarily once the
 *     window has been opened -- and a caller has to be able to see that rather than discover it
 *     by failing.
 *  2. The base carries the factory that returns the instance for a named backend. It is declared
 *     here and defined where the backends are known, because this layer must not reach for them.
 *  3. Each backend manages its own lifecycle; the caller only asks it to initialize and to shut
 *     down, and the object it gets back owns whatever it opened.
 *  4. `initialize` rather than `establish`: what happens is a state change in the capability, not
 *     the raising of a connection.
 *  5. Capabilities carry parameters, and those parameters have their own interface so a backend
 *     can define what it needs -- for a write through 43499 that is a route and a route offset,
 *     which are not facts the base could have known.
 *
 *  Why the virtual path is allowed here: virtual dispatch enters the PI window, and the window
 *  prohibition protects against issuing a syscall or making an indirect call inside it. A call
 *  from outside is safe, and the machine check that banned vtables in the binary was replaced by
 *  one that states that property directly. Confirmation, reason and debt: ADR-0014 annex.
 */
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace ghostlock::contract {
    /** Which channel a read or write travels over. */
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
        KernelPage = 1,
        FilePage = 2,
        ModuleWindow = 3,
    };

    /** Where a capability stands. Not a boolean, because a capability that arrives mid-run is
     *  the normal case here rather than the exception. */
    enum class AccessState : std::uint8_t {
        /** This backend does not have it and will not. */
        Unsupported = 0,
        /** It exists and has not been brought up yet. */
        Declared = 1,
        /** Being brought up. */
        Initializing = 2,
        /** Usable now. */
        Ready = 3,
        /** It was brought up and stopped working; the reason travels in the status. */
        Failed = 4,
    };

    [[nodiscard]] constexpr const char *access_state_name(AccessState state) noexcept {
        switch (state) {
            case AccessState::Unsupported: return "unsupported";
            case AccessState::Declared: return "declared";
            case AccessState::Initializing: return "initializing";
            case AccessState::Ready: return "ok";
            case AccessState::Failed: return "call-failed";
        }
        return "unknown";
    }

    /** How one operation ended. */
    struct AccessStatus final {
        enum class Code : std::uint8_t {
            Ok = 0,
            /** The address is not one this implementation may write. */
            OutOfRange = 1,
            /** Not initialized yet, or already shut down. */
            NotInitialized = 2,
            /** This implementation does not have it. */
            Unsupported = 3,
            /** Attempted and failed downstream. */
            Failed = 4,
            /** The parameters do not fit this operation. */
            BadParameters = 5,
        };
        Code code = Code::Unsupported;

        [[nodiscard]] constexpr bool ok() const noexcept { return code == Code::Ok; }
    };

    [[nodiscard]] constexpr const char *access_status_name(AccessStatus status) noexcept {
        switch (status.code) {
            case AccessStatus::Code::Ok: return "ok";
            case AccessStatus::Code::OutOfRange: return "out-of-range";
            case AccessStatus::Code::NotInitialized: return "not-initialized";
            case AccessStatus::Code::Unsupported: return "unsupported";
            case AccessStatus::Code::Failed: return "call-failed";
            case AccessStatus::Code::BadParameters: return "bad-parameters";
        }
        return "unknown";
    }

    /** What a read produced, or why it did not. */
    struct ReadResult final {
        AccessStatus status{};
        std::uint64_t value = 0U;
    };

    /** Parameters a capability needs and the base could not have known. A backend defines its
     *  own, and says so by its name: the operations take this base, and an implementation
     *  refuses anything whose name it does not recognise rather than guessing. For a write
     *  through 43499 that is a route and a route offset, which is exactly the sort of thing that
     *  must not be baked into this layer. */
    class AccessParameters {
    public:
        AccessParameters() noexcept = default;
        AccessParameters(const AccessParameters &) = delete;
        AccessParameters &operator=(const AccessParameters &) = delete;
        virtual ~AccessParameters() noexcept = default;

        /** What the implementing backend calls this parameter set. The operation checks it and
         *  answers bad-parameters when it does not match. */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    };

    /** The interface. Implementers write the pure virtuals; the convenient forms are inherited,
     *  and so is the lifecycle framing. */
    class KernelAccess {
    public:
        KernelAccess() noexcept = default;
        KernelAccess(const KernelAccess &) = delete;
        KernelAccess &operator=(const KernelAccess &) = delete;
        virtual ~KernelAccess() noexcept = default;

        /** The factory: the instance for a named backend, or null when this build has none. It
         *  is declared here because this is where a caller looks for it, and defined where the
         *  backends are known, because this layer must not reach for them. */
        [[nodiscard]] static std::unique_ptr<KernelAccess> create(std::string_view backend) noexcept;

        /** Where this capability stands right now. */
        [[nodiscard]] virtual AccessState state(AccessChannel channel) const noexcept = 0;
        /** Whether this backend has the channel at all, whatever state it is in. */
        [[nodiscard]] virtual bool supports(AccessChannel channel) const noexcept = 0;
        [[nodiscard]] virtual bool supports(AccessCarrier carrier) const noexcept = 0;

        /** Bring the capability up. The backend owns what it opens; the caller only asks. */
        [[nodiscard]] virtual AccessStatus initialize(const AccessParameters &parameters) noexcept = 0;
        /** Shut everything down and release it. */
        [[nodiscard]] virtual AccessStatus shutdown() noexcept = 0;

        [[nodiscard]] virtual AccessStatus read(std::uint64_t address, std::span<std::byte> out,
                                                 AccessChannel channel,
                                                 const AccessParameters &parameters) noexcept = 0;
        [[nodiscard]] virtual AccessStatus write(std::uint64_t address, std::span<const std::byte> in,
                                                  AccessChannel channel,
                                                  const AccessParameters &parameters) noexcept = 0;
        [[nodiscard]] virtual AccessStatus write_zero(std::uint64_t address, AccessChannel channel,
                                                      const AccessParameters &parameters) noexcept = 0;

        /** One word read. Not virtual: implementers write `read`. */
        [[nodiscard]] ReadResult read64(std::uint64_t address, AccessChannel channel,
                                        const AccessParameters &parameters) noexcept {
            std::byte raw[sizeof(std::uint64_t)] = {};
            const AccessStatus status = read(address, std::span<std::byte>(raw), channel, parameters);
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

        /** One word write. Not virtual, same reason. */
        [[nodiscard]] AccessStatus write64(std::uint64_t address, std::uint64_t value,
                                           AccessChannel channel,
                                           const AccessParameters &parameters) noexcept {
            std::byte raw[sizeof(std::uint64_t)] = {};
            for (std::size_t i = 0U; i < sizeof(std::uint64_t); ++i) {
                raw[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
            }
            return write(address, std::span<const std::byte>(raw), channel, parameters);
        }
    };

    /** Parameters that carry nothing, for calls that need none. Named, so an implementation
     *  that expects its own set refuses it rather than treating the absence as a default. */
    class NoParameters final : public AccessParameters {
    public:
        [[nodiscard]] std::string_view name() const noexcept override { return "none"; }
    };
}  // namespace ghostlock::contract
