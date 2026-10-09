/** @file tests/kernel_access_test.cpp
 *  The interface against a fake implementation that supports one channel and nothing else.
 *
 *  Two things are being pinned. The convenience layer has to be usable without being
 *  implemented: the fake writes `read` and `write` and gets `read64` and `write64` for free,
 *  which is the whole reason those are non-virtual. And an implementation that does not have a
 *  channel must say so rather than pretending, so the fake answers a channel it does not support
 *  with the refusal code and the test drives that path too.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "contract/kernel_access.hpp"

namespace {
    using ghostlock::contract::AccessCarrier;
    using ghostlock::contract::AccessChannel;
    using ghostlock::contract::AccessStatus;
    using ghostlock::contract::KernelAccess;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** One page of pretend kernel memory, reachable over one channel only. */
    class FakeAccess final : public KernelAccess {
    public:
        static constexpr std::uint64_t kBase = 0xffff800000100000ULL;
        std::uint8_t bytes[64] = {};
        std::size_t writes = 0U;

        AccessStatus read(std::uint64_t address, std::span<std::byte> out,
                          AccessChannel channel) noexcept override {
            if (channel != AccessChannel::PiWindow) {
                return AccessStatus{AccessStatus::Code::Unsupported};
            }
            if (address < kBase || address + out.size() > kBase + sizeof(bytes)) {
                return AccessStatus{AccessStatus::Code::OutOfRange};
            }
            for (std::size_t i = 0U; i < out.size(); ++i) {
                out[i] = static_cast<std::byte>(bytes[address - kBase + i]);
            }
            return AccessStatus{AccessStatus::Code::Ok};
        }

        AccessStatus write(std::uint64_t address, std::span<const std::byte> in,
                           AccessChannel channel) noexcept override {
            if (channel != AccessChannel::PiWindow) {
                return AccessStatus{AccessStatus::Code::Unsupported};
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

        AccessStatus write_zero(std::uint64_t address) noexcept override {
            const std::byte zero[8] = {};
            return write(address, std::span<const std::byte>(zero), AccessChannel::PiWindow);
        }

        [[nodiscard]] bool supports(AccessChannel channel) const noexcept override {
            return channel == AccessChannel::PiWindow;
        }
        [[nodiscard]] bool supports(AccessCarrier carrier) const noexcept override {
            return carrier == AccessCarrier::KernelPage;
        }
        [[nodiscard]] bool available() const noexcept override { return true; }
    };

    void the_convenience_layer_is_free() {
        FakeAccess access;
        const AccessStatus written = access.write64(FakeAccess::kBase + 8U, 0x4142434445464748ULL,
                                                    AccessChannel::PiWindow);
        expect(written.ok(), "a word writes through the layer the implementer did not write");
        expect(access.writes == 1U, "and it reached the implementation once");
        const auto back = access.read64(FakeAccess::kBase + 8U, AccessChannel::PiWindow);
        expect(back.status.ok() && back.value == 0x4142434445464748ULL,
               "and reads back the same word, little-endian on the way in and out");
        const AccessStatus up = access.establish(AccessChannel::PiWindow, AccessCarrier::KernelPage);
        const AccessStatus down = access.close(AccessCarrier::KernelPage);
        expect(up.ok() && down.ok(), "a default establish and close succeed");
    }

    void a_missing_channel_says_so() {
        FakeAccess access;
        const auto via_module = access.read64(FakeAccess::kBase, AccessChannel::KernelModule);
        expect(via_module.status.code == AccessStatus::Code::Unsupported,
               "a channel this implementation does not have is refused, not faked");
        expect(via_module.value == 0U, "and no value comes back with the refusal");
        expect(access.supports(AccessChannel::PiWindow), "the channel it does have is reported");
        expect(!access.supports(AccessCarrier::FilePage), "and the carrier it does not is not");
        expect(access.available(), "while the implementation itself is available");
        const auto outside = access.write64(0x1000U, 1U, AccessChannel::PiWindow);
        expect(outside.code == AccessStatus::Code::OutOfRange, "an address outside its range is refused");
        expect(std::strcmp(access_status_name(outside), "out-of-range") == 0,
               "under the name a caller acts on");
    }
}  // namespace

int main() {
    std::printf("kernel access: the layer implementers do not write\n");
    the_convenience_layer_is_free();
    std::printf("kernel access: what a missing channel reports\n");
    a_missing_channel_says_so();
    if (g_failures != 0U) {
        std::printf("kernel access: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("kernel access: PASS (read and write to implement, the rest inherited)\n");
    return 0;
}
