#pragma once
/** @file contract/kernel_access.hpp
 *  The one thing a script may reach: kernel memory, by address.
 *
 *  Architecture ruling (ADR-0014) and the refinements that followed it, all in one class:
 *  everything a caller needs is nested inside KernelAccess, so the surface is one name.
 *
 *  The shape of a call: an address, the bytes to write, a channel, and a bag of misc
 *  parameters. The address and the value are the operation's own arguments, which is why the
 *  parameter object is named for the rest -- AccessMiscParameters -- and carries what only a
 *  particular backend knows, such as the 43499 route and the offset in it.
 *
 *  Everything returns AccessResult<T>: a status and a value, generically. A read hands back the
 *  bytes it read as a span rather than taking a buffer to fill, and a write hands back how many
 *  bytes it wrote, so a backend that can only write a whole word says so with the number instead
 *  of pretending. Width is a capability: supports_width() answers what a backend can do, because
 *  the 43499 path begins able to write one whole 64-bit word and only later able to write less.
 */
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "contract/kind.hpp"

namespace ghostlock::contract {
    /** Which channel a read or write travels over. */
    enum class AccessChannel : std::uint8_t {
        Unavailable = 0,
        /** PI-futex: the write lands through the waiter window. */
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

    class KernelAccess {
    public:
        /** How a call ended. */
        enum class Status : std::uint8_t {
            Ok = 0,
            /** The address is not one this implementation may touch. */
            OutOfRange = 1,
            /** Not initialized yet, or already shut down. */
            NotInitialized = 2,
            /** This implementation does not have the channel, the carrier or the width. */
            Unsupported = 3,
            /** Attempted and failed downstream. */
            Failed = 4,
            /** The misc parameters are not the set this operation recognises. */
            BadParameters = 5,
            /** The width asked for is one this implementation cannot do. */
            BadWidth = 6,
        };

        [[nodiscard]] static constexpr const char *status_name(Status status) noexcept {
            switch (status) {
                case Status::Ok: return "ok";
                case Status::OutOfRange: return "out-of-range";
                case Status::NotInitialized: return "not-initialized";
                case Status::Unsupported: return "unsupported";
                case Status::Failed: return "call-failed";
                case Status::BadParameters: return "bad-parameters";
                case Status::BadWidth: return "bad-width";
            }
            return "unknown";
        }

        /** A status and a value. The value is the operation's answer: the bytes a read read,
         *  the count a write wrote, the word a word read read. `value` is default-constructed
         *  and only meaningful when ok(), so a caller checks before it looks. */
        template <class Value>
        struct AccessResult final {
            Status status = Status::Unsupported;
            Value value{};

            /** A successful answer carrying its value. */
            [[nodiscard]] static constexpr AccessResult ok(Value answer) noexcept {
                return AccessResult{Status::Ok, answer};
            }
            /** A refusal carrying nothing. Why is the status. */
            [[nodiscard]] static constexpr AccessResult failed(Status why) noexcept {
                return AccessResult{why, Value{}};
            }

            [[nodiscard]] constexpr bool ok() const noexcept { return status == Status::Ok; }
        };

        /** Where a capability stands. Not a boolean: a capability that arrives mid-run is the
         *  normal case here. The 43499 path writes early and can only read arbitrarily once the
         *  window is open. */
        enum class AccessState : std::uint8_t {
            /** This backend does not have it and will not. */
            Unsupported = 0,
            /** It exists and has not been brought up. */
            Declared = 1,
            /** Being brought up. */
            Initializing = 2,
            /** Usable now. */
            Ready = 3,
            /** It was up and stopped working; the status of the last call says why. */
            Failed = 4,
            /** It was up, and was deliberately taken down. Different from Declared, which never
             *  came up, and from Unsupported, which never existed. A backend whose carrier is a
             *  loaded module ends here when the module is unloaded. */
            Closed = 5,
        };

        [[nodiscard]] static constexpr const char *state_name(AccessState state) noexcept {
            switch (state) {
                case AccessState::Unsupported: return "unsupported";
                case AccessState::Declared: return "declared";
                case AccessState::Initializing: return "initializing";
                case AccessState::Ready: return "ok";
                case AccessState::Failed: return "call-failed";
                case AccessState::Closed: return "closed";
            }
            return "unknown";
        }

        /** The rest of what an operation needs, beyond the address and the value: a route and an
         *  offset for the 43499 path, whatever another backend invents for itself. Named, so an
         *  operation that does not recognise the set refuses it instead of guessing. */
        class AccessMiscParameters {
        public:
            AccessMiscParameters() noexcept = default;
            AccessMiscParameters(const AccessMiscParameters &) = delete;
            AccessMiscParameters &operator=(const AccessMiscParameters &) = delete;
            virtual ~AccessMiscParameters() noexcept = default;

            [[nodiscard]] virtual std::string_view name() const noexcept = 0;
        };

        /** Parameters that carry nothing, for calls that need none. Named, so an implementation
         *  expecting its own set refuses it rather than reading absence as a default. */
        class NoMiscParameters final : public AccessMiscParameters {
        public:
            [[nodiscard]] std::string_view name() const noexcept override { return "none"; }
        };

        KernelAccess() noexcept = default;
        KernelAccess(const KernelAccess &) = delete;
        KernelAccess &operator=(const KernelAccess &) = delete;
        virtual ~KernelAccess() noexcept = default;

        /** The factory: the instance for a backend, or null when this build has none. The backend
         *  is an id rather than a string, because the tokens already have an authority in the
         *  component catalogue and an enum bound to it is how a caller names one without
         *  inventing a spelling. Declared here and defined where the backends are known, since
         *  this layer must not reach for them. */
        [[nodiscard]] static std::unique_ptr<KernelAccess> create(BackendId backend) noexcept;

        /** Where a capability stands right now. */
        [[nodiscard]] virtual AccessState state(AccessChannel channel) const noexcept = 0;
        /** Whether this backend has the channel at all, whatever state it is in. */
        [[nodiscard]] virtual bool supports(AccessChannel channel) const noexcept = 0;
        [[nodiscard]] virtual bool supports(AccessCarrier carrier) const noexcept = 0;
        /** Which widths this backend can write and read. A backend that can only handle one
         *  whole word answers yes for that width and no for the others. */
        [[nodiscard]] virtual bool supports_width(std::size_t bytes) const noexcept = 0;

        /** Bring the capability up. The backend owns what it opens; the caller only asks. */
        [[nodiscard]] virtual AccessResult<bool> initialize(
            const AccessMiscParameters &parameters) noexcept = 0;
        /** Shut everything down and release it. */
        [[nodiscard]] virtual AccessResult<bool> shutdown() noexcept = 0;

        /** Read bytes. The answer is the bytes themselves: the span points into storage the
         *  implementation owns and stays valid until the next call, so a caller copies what it
         *  wants to keep rather than handing over a buffer to fill. */
        [[nodiscard]] virtual AccessResult<std::span<const std::byte>> read(
            std::uint64_t address, std::size_t bytes, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept = 0;

        /** Write bytes, and answer with how many were written. A backend that can only write a
         *  whole word does it and says so with the number. */
        [[nodiscard]] virtual AccessResult<std::size_t> write(
            std::uint64_t address, std::span<const std::byte> in, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept = 0;

        /** One word read. Virtual because an implementation may well be able to do this and
         *  nothing else, and then this is the whole of its read side. The default builds it out
         *  of read(); an implementation that can do better overrides it. */
        [[nodiscard]] virtual AccessResult<std::uint64_t> read64(
            std::uint64_t address, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept;

        /** One word write. Virtual for the mirror reason: writing one word is what a young
         *  implementation can do, and it is the width it can do it at. */
        [[nodiscard]] virtual AccessResult<std::size_t> write64(
            std::uint64_t address, std::uint64_t value, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept;

        /** Write one zero word, the bootstrap every later write needs. */
        [[nodiscard]] virtual AccessResult<std::size_t> write_zero(
            std::uint64_t address, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept;
    };

    /** The default word read: eight bytes through read(), assembled little-endian. */
    [[nodiscard]] inline KernelAccess::AccessResult<std::uint64_t> KernelAccess::read64(
        std::uint64_t address, AccessChannel channel, const AccessMiscParameters &parameters) noexcept {
        AccessResult<std::uint64_t> result{};
        const auto bytes = read(address, sizeof(std::uint64_t), channel, parameters);
        result.status = bytes.status;
        if (!bytes.ok()) {
            return result;
        }
        std::uint64_t value = 0U;
        for (std::size_t i = 0U; i < bytes.value.size() && i < sizeof(std::uint64_t); ++i) {
            value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(bytes.value[i])) << (8U * i);
        }
        result.status = Status::Ok;
        result.value = value;
        return result;
    }

    /** The default word write: eight bytes through write(), little-endian. */
    [[nodiscard]] inline KernelAccess::AccessResult<std::size_t> KernelAccess::write64(
        std::uint64_t address, std::uint64_t value, AccessChannel channel,
        const AccessMiscParameters &parameters) noexcept {
        std::byte raw[sizeof(std::uint64_t)] = {};
        for (std::size_t i = 0U; i < sizeof(std::uint64_t); ++i) {
            raw[i] = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
        }
        return write(address, std::span<const std::byte>(raw), channel, parameters);
    }

    /** The zero write, as the word write of zero. */
    [[nodiscard]] inline KernelAccess::AccessResult<std::size_t> KernelAccess::write_zero(
        std::uint64_t address, AccessChannel channel, const AccessMiscParameters &parameters) noexcept {
        return write64(address, 0U, channel, parameters);
    }
}  // namespace ghostlock::contract
