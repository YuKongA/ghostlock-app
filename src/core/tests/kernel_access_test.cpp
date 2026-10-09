/** @file tests/kernel_access_test.cpp
 *  A fake backend that can only write one whole word, which is what a young implementation
 *  looks like.
 *
 *  It pins the things the interface promises. Width is a capability, so the fake answers yes
 *  for eight bytes and no for four, and a four-byte write is refused by name rather than done
 *  anyway. A read hands the bytes back rather than filling a buffer, and a write hands back how
 *  many bytes it wrote. State is reported rather than assumed, so using it before initialize is
 *  refused by name. And the word forms are virtual with working defaults: this fake overrides
 *  read64 and inherits write64, and both are exercised.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "contract/kernel_access.hpp"

namespace {
    using ghostlock::contract::AccessCarrier;
    using ghostlock::contract::AccessChannel;
    using ghostlock::contract::KernelAccess;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The misc parameters this backend wants: a route and an offset in it. */
    class RouteMisc final : public KernelAccess::AccessMiscParameters {
    public:
        std::uint64_t route = 0U;
        std::uint64_t offset = 0U;
        [[nodiscard]] std::string_view name() const noexcept override { return "route"; }
    };

    class FakeAccess final : public KernelAccess {
    public:
        static constexpr std::uint64_t kBase = 0xffff800000100000ULL;
        std::uint8_t bytes[64] = {};
        std::size_t word_writes = 0U;
        bool initialized = false;
        std::byte window[64] = {};

        [[nodiscard]] AccessState state(AccessChannel channel) const noexcept override {
            if (channel != AccessChannel::PiWindow) {
                return KernelAccess::AccessState::Unsupported;
            }
            return initialized ? KernelAccess::AccessState::Ready : KernelAccess::AccessState::Declared;
        }
        [[nodiscard]] bool supports(AccessChannel channel) const noexcept override {
            return channel == AccessChannel::PiWindow;
        }
        [[nodiscard]] bool supports(AccessCarrier carrier) const noexcept override {
            return carrier == AccessCarrier::KernelPage;
        }
        [[nodiscard]] bool supports_width(std::size_t bytes) const noexcept override {
            return bytes == sizeof(std::uint64_t);
        }

        [[nodiscard]] AccessResult<bool> initialize(
            const AccessMiscParameters &parameters) noexcept override {
            AccessResult<bool> result{};
            if (parameters.name() != std::string_view("route")) {
                result.status = KernelAccess::Status::BadParameters;
                return result;
            }
            initialized = true;
            result.status = KernelAccess::Status::Ok;
            result.value = true;
            return result;
        }
        [[nodiscard]] AccessResult<bool> shutdown() noexcept override {
            initialized = false;
            AccessResult<bool> result{};
            result.status = KernelAccess::Status::Ok;
            result.value = true;
            return result;
        }

        [[nodiscard]] AccessResult<std::span<const std::byte>> read(
            std::uint64_t address, std::size_t count, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept override {
            AccessResult<std::span<const std::byte>> result{};
            if (!initialized) {
                result.status = KernelAccess::Status::NotInitialized;
                return result;
            }
            if (channel != AccessChannel::PiWindow || parameters.name() != std::string_view("route")) {
                result.status = KernelAccess::Status::BadParameters;
                return result;
            }
            if (!supports_width(count)) {
                result.status = KernelAccess::Status::BadWidth;
                return result;
            }
            if (address < kBase || address + count > kBase + sizeof(bytes)) {
                result.status = KernelAccess::Status::OutOfRange;
                return result;
            }
            for (std::size_t i = 0U; i < count; ++i) {
                window[i] = static_cast<std::byte>(bytes_at(address + i));
            }
            result.status = KernelAccess::Status::Ok;
            result.value = std::span<const std::byte>(window, count);
            return result;
        }

        [[nodiscard]] AccessResult<std::size_t> write(
            std::uint64_t address, std::span<const std::byte> in, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept override {
            AccessResult<std::size_t> result{};
            if (!initialized) {
                result.status = KernelAccess::Status::NotInitialized;
                return result;
            }
            if (channel != AccessChannel::PiWindow || parameters.name() != std::string_view("route")) {
                result.status = KernelAccess::Status::BadParameters;
                return result;
            }
            if (!supports_width(in.size())) {
                result.status = KernelAccess::Status::BadWidth;
                return result;
            }
            if (address < kBase || address + in.size() > kBase + sizeof(bytes)) {
                result.status = KernelAccess::Status::OutOfRange;
                return result;
            }
            for (std::size_t i = 0U; i < in.size(); ++i) {
                bytes[address - kBase + i] = std::to_integer<std::uint8_t>(in[i]);
            }
            result.status = KernelAccess::Status::Ok;
            result.value = in.size();
            ++word_writes;
            return result;
        }

        /** The whole of this backend read side: it can read one word and no less, so it
         *  overrides the word form instead of inheriting the one built out of read(). */
        [[nodiscard]] AccessResult<std::uint64_t> read64(
            std::uint64_t address, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept override {
            AccessResult<std::uint64_t> result{};
            const auto got = read(address, sizeof(std::uint64_t), channel, parameters);
            result.status = got.ok() ? KernelAccess::Status::Ok : got.status;
            if (got.ok()) {
                std::uint64_t value = 0U;
                for (std::size_t i = 0U; i < got.value.size(); ++i) {
                    value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(got.value[i])) << (8U * i);
                }
                result.value = value;
            }
            return result;
        }

    private:
        [[nodiscard]] std::uint8_t bytes_at(std::uint64_t address) const noexcept {
            return bytes[address - kBase];
        }
    };

    void width_is_a_capability() {
        FakeAccess access;
        RouteMisc route;
        expect(access.supports_width(8U), "this backend writes a whole word");
        expect(!access.supports_width(4U), "and not four bytes");
        const auto up = access.initialize(route);
        expect(up.ok(), "it comes up with the parameters it wants");
        const std::byte four[4] = {};
        const auto narrow = access.write(FakeAccess::kBase, std::span<const std::byte>(four),
                                         AccessChannel::PiWindow, route);
        expect(narrow.status == KernelAccess::Status::BadWidth,
               "a width it cannot do is refused by name rather than written anyway");
    }

    void the_answer_carries_the_value() {
        FakeAccess access;
        RouteMisc route;
        static_cast<void>(access.initialize(route));
        const auto wrote = access.write64(FakeAccess::kBase + 8U, 0x4142434445464748ULL,
                                          AccessChannel::PiWindow, route);
        expect(wrote.ok() && wrote.value == 8U, "a write answers with how many bytes it wrote");
        const auto read = access.read(FakeAccess::kBase + 8U, 8U, AccessChannel::PiWindow, route);
        expect(read.ok() && read.value.size() == 8U,
               "and a read hands back the bytes rather than filling a buffer");
        if (read.ok() && read.value.size() == 8U) {
            expect(std::to_integer<std::uint8_t>(read.value[0]) == 0x48U,
                   "with the little-endian first byte where it belongs");
        }
        const auto word = access.read64(FakeAccess::kBase + 8U, AccessChannel::PiWindow, route);
        expect(word.ok() && word.value == 0x4142434445464748ULL,
               "and the virtual word read answers with the word");
        static_cast<void>(access.write_zero(FakeAccess::kBase, AccessChannel::PiWindow, route));
        const auto cleared = access.read64(FakeAccess::kBase, AccessChannel::PiWindow, route);
        expect(cleared.ok() && cleared.value == 0U, "and the zero write goes through the same path");
    }

    void state_and_parameters_are_named_refusals() {
        FakeAccess access;
        RouteMisc route;
        const auto early = access.write64(FakeAccess::kBase, 1U, AccessChannel::PiWindow, route);
        expect(early.status == KernelAccess::Status::NotInitialized, "before initialize, using it is refused by name");
        const KernelAccess::NoMiscParameters none;
        expect(access.initialize(none).status == KernelAccess::Status::BadParameters,
               "and initializing with a parameter set this backend does not know is refused too");
        static_cast<void>(access.initialize(route));
        expect(access.state(AccessChannel::PiWindow) == KernelAccess::AccessState::Ready, "then it reports ready");
        expect(access.state(AccessChannel::KernelModule) == KernelAccess::AccessState::Unsupported,
               "while a channel it does not have reports unsupported");
        static_cast<void>(access.shutdown());
        expect(access.state(AccessChannel::PiWindow) == KernelAccess::AccessState::Declared,
               "and shutting down returns it to declared rather than unsupported");
        expect(std::strcmp(KernelAccess::status_name(KernelAccess::Status::BadWidth), "bad-width") == 0,
               "every status has the name a caller acts on");
    }
}  // namespace

int main() {
    std::printf("kernel access: width is a capability\n");
    width_is_a_capability();
    std::printf("kernel access: the answer carries the value\n");
    the_answer_carries_the_value();
    std::printf("kernel access: named refusals\n");
    state_and_parameters_are_named_refusals();
    if (g_failures != 0U) {
        std::printf("kernel access: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("kernel access: PASS (width, results, named refusals)\n");
    return 0;
}
