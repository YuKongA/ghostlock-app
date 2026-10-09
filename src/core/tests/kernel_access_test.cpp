/** @file tests/kernel_access_test.cpp
 *  The interface against a fake backend that comes up in two stages and wants parameters.
 *
 *  Three things are pinned. A capability reports where it is rather than whether it exists, so
 *  the fake is declared before it is initialized and ready after, and using it in between is
 *  refused by name. Parameters are the backend's own, so the fake accepts its own set and refuses
 *  another by name rather than guessing. And the convenient word forms are inherited, so an
 *  implementer writes read, write and write_zero and gets read64 and write64 for free.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "contract/kernel_access.hpp"

namespace {
    using ghostlock::contract::AccessCarrier;
    using ghostlock::contract::AccessChannel;
    using ghostlock::contract::AccessParameters;
    using ghostlock::contract::AccessState;
    using ghostlock::contract::AccessStatus;
    using ghostlock::contract::KernelAccess;
    using ghostlock::contract::NoParameters;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** What this backend's write needs: a route and where in that route to write. The base
     *  could not have known either of these, which is the reason the interface exists. */
    class RouteParameters final : public AccessParameters {
    public:
        std::uint64_t route = 0U;
        std::uint64_t offset = 0U;
        [[nodiscard]] std::string_view name() const noexcept override { return "route"; }
    };

    /** A backend that has one channel, comes up in one step, and wants route parameters. */
    class FakeAccess final : public KernelAccess {
    public:
        static constexpr std::uint64_t kBase = 0xffff800000100000ULL;
        std::uint8_t bytes[64] = {};
        std::size_t writes = 0U;
        bool initialized = false;

        [[nodiscard]] AccessState state(AccessChannel channel) const noexcept override {
            if (channel != AccessChannel::PiWindow) {
                return AccessState::Unsupported;
            }
            return initialized ? AccessState::Ready : AccessState::Declared;
        }
        [[nodiscard]] bool supports(AccessChannel channel) const noexcept override {
            return channel == AccessChannel::PiWindow;
        }
        [[nodiscard]] bool supports(AccessCarrier carrier) const noexcept override {
            return carrier == AccessCarrier::KernelPage;
        }

        [[nodiscard]] AccessStatus initialize(const AccessParameters &parameters) noexcept override {
            if (parameters.name() != std::string_view("route")) {
                return AccessStatus{AccessStatus::Code::BadParameters};
            }
            initialized = true;
            return AccessStatus{AccessStatus::Code::Ok};
        }
        [[nodiscard]] AccessStatus shutdown() noexcept override {
            initialized = false;
            return AccessStatus{AccessStatus::Code::Ok};
        }

        [[nodiscard]] AccessStatus read(std::uint64_t address, std::span<std::byte> out,
                                        AccessChannel channel,
                                        const AccessParameters &parameters) noexcept override {
            if (!initialized) {
                return AccessStatus{AccessStatus::Code::NotInitialized};
            }
            if (channel != AccessChannel::PiWindow || parameters.name() != std::string_view("route")) {
                return AccessStatus{AccessStatus::Code::BadParameters};
            }
            if (address < kBase || address + out.size() > kBase + sizeof(bytes)) {
                return AccessStatus{AccessStatus::Code::OutOfRange};
            }
            for (std::size_t i = 0U; i < out.size(); ++i) {
                out[i] = static_cast<std::byte>(bytes[address - kBase + i]);
            }
            return AccessStatus{AccessStatus::Code::Ok};
        }

        [[nodiscard]] AccessStatus write(std::uint64_t address, std::span<const std::byte> in,
                                         AccessChannel channel,
                                         const AccessParameters &parameters) noexcept override {
            if (!initialized) {
                return AccessStatus{AccessStatus::Code::NotInitialized};
            }
            if (channel != AccessChannel::PiWindow || parameters.name() != std::string_view("route")) {
                return AccessStatus{AccessStatus::Code::BadParameters};
            }
            if (address < kBase || address + in.size() > kBase + sizeof(bytes)) {
                return AccessStatus{AccessStatus::Code::OutOfRange};
            }
            for (std::size_t i = 0U; i < in.size(); ++i) {
                bytes[address - kBase + i] = std::to_integer<std::uint8_t>(in[i]);
            }
            ++writes;
            return AccessStatus{AccessStatus::Code::Ok};
        }

        [[nodiscard]] AccessStatus write_zero(std::uint64_t address, AccessChannel channel,
                                              const AccessParameters &parameters) noexcept override {
            const std::byte zero[8] = {};
            return write(address, std::span<const std::byte>(zero), channel, parameters);
        }
    };

    void a_capability_reports_where_it_is() {
        FakeAccess access;
        RouteParameters route;
        route.route = 2U;
        route.offset = 0x40U;
        expect(access.state(AccessChannel::PiWindow) == AccessState::Declared,
               "before anything is done the channel exists and is not up");
        expect(access.state(AccessChannel::KernelModule) == AccessState::Unsupported,
               "and a channel this backend does not have says so");
        const auto early = access.write64(FakeAccess::kBase, 1U, AccessChannel::PiWindow, route);
        expect(early.code == AccessStatus::Code::NotInitialized,
               "using it before it is up is refused by name, not by failing downstream");
        const NoParameters wrong;
        expect(access.initialize(wrong).code == AccessStatus::Code::BadParameters,
               "and initializing with parameters this backend does not recognise is refused too");
        expect(!access.initialized, "which leaves it where it was");
        const AccessStatus up = access.initialize(route);
        expect(up.ok() && access.state(AccessChannel::PiWindow) == AccessState::Ready,
               "the right parameters bring it up, and the state says so");
    }

    void the_word_forms_are_inherited() {
        FakeAccess access;
        RouteParameters route;
        static_cast<void>(access.initialize(route));
        expect(access.write64(FakeAccess::kBase + 8U, 0x4142434445464748ULL, AccessChannel::PiWindow, route).ok(),
               "a word writes through the layer the implementer did not write");
        expect(access.writes == 1U, "and reaches the implementation once");
        const auto back = access.read64(FakeAccess::kBase + 8U, AccessChannel::PiWindow, route);
        expect(back.status.ok() && back.value == 0x4142434445464748ULL,
               "and reads back the same word");
        expect(access.write_zero(FakeAccess::kBase, AccessChannel::PiWindow, route).ok(),
               "and the zero write is the same path with a zero in it");
        const auto outside = access.write64(0x1000U, 1U, AccessChannel::PiWindow, route);
        expect(outside.code == AccessStatus::Code::OutOfRange, "an address outside its range is refused");
        expect(std::strcmp(access_status_name(outside), "out-of-range") == 0,
               "under the name a caller acts on");
        static_cast<void>(access.shutdown());
        expect(access.state(AccessChannel::PiWindow) == AccessState::Declared,
               "and shutting down puts it back to declared rather than unsupported");
    }
}  // namespace

int main() {
    std::printf("kernel access: where a capability stands\n");
    a_capability_reports_where_it_is();
    std::printf("kernel access: the word forms are inherited\n");
    the_word_forms_are_inherited();
    if (g_failures != 0U) {
        std::printf("kernel access: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("kernel access: PASS (state, parameters, and the inherited forms)\n");
    return 0;
}
