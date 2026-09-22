#include "target.h"

using namespace ghostlock;

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>



static_assert(KIMAGE_TEXT_BASE == 0xffffffc080000000ULL);
static_assert(MTK_VADDR_BASE == 0xffffffc000000000ULL);
static_assert(P0_PAGE_OFFSET == 0xffffff8000000000ULL);
static_assert(P0_PHYS_OFFSET == 0x80000000ULL);
static_assert(KERNELSNITCH_IDENTITY_START == 0xffffff8000000000ULL);
static_assert(KERNELSNITCH_IDENTITY_END == 0xffffff8c00000000ULL);
static_assert(DIRECT_MAP_BASE == 0xffffff8000000000ULL);
static_assert(DIRECT_MAP_END == 0xffffff9000000000ULL);
static_assert(VMEMMAP_START == 0xfffffffe00000000ULL);
static_assert(LOCK_OFF == 0x0e80);
static_assert(W0_OFF == 0x1180);
static_assert(FOPS_OFF == 0x0f80);
static_assert(RIGHT_OFF == 0x1240);
static_assert(LEFT_OFF == 0x1260);
static_assert(FAKE_TASK_OFF == 0x1280);
static_assert(CRED_COPY_OFF == 0x1080);
static_assert(TCP_FAKE_TASK_OFF == 0x5800);
static_assert(TCP_CRED_COPY_OFF == 0x6800);

/* Compatibility vectors for target.h values deliberately left profile-owned.
 * They prove CPP01 did not silently reinterpret device/kernel data as C++
 * compile-time authority. */
static_assert(P0_KERNEL_PHYS_LOAD == 0xa8000000ULL);
static_assert(QC_GKI_6_12_PHYS_LOAD == 0xc7800000ULL);
static_assert(XRING_KERNEL_PHYS_LOAD == 0x80200000ULL);
static_assert(INIT_TASK_OFF == 0x0211e280ULL);
static_assert(INIT_CRED_OFF == 0x02130748ULL);
static_assert(ROOT_TASK_GROUP_OFF == 0x02317580ULL);
static_assert(SELINUX_ENFORCING_OFF == 0x02358ee0ULL);
static_assert(SELINUX_BLOB_SIZES_OFF == 0x0167b5d0ULL);
static_assert(SECURITY_HOOK_HEADS_OFF == 0);
static_assert(SLIDE_NFULNL_LOGGER_OFF == 0x02112260ULL);
static_assert(SLIDE_LOGGERS_0_1_OFF == 0x021121b0ULL);
static_assert(SLIDE_RANDOM_BOOT_ID_DATA_OFF == 0x02379ed8ULL);
static_assert(SLIDE_SYSCTL_BOOTID_OFF == 0x02379ed8ULL);
static_assert(PSELECT_WAITER_WORD_SHIFT == -2);
static_assert(FAKE_WAITER_TREE_PRIO_OFF == 0x18);
static_assert(FAKE_WAITER_TREE_DEADLINE_OFF == 0x20);
static_assert(FAKE_WAITER_PI_TREE_ENTRY_OFF == 0x28);
static_assert(FAKE_WAITER_PI_TREE_PRIO_OFF == 0x40);
static_assert(FAKE_WAITER_PI_TREE_DEADLINE_OFF == 0x48);
static_assert(FAKE_WAITER_TASK_OFF == 0x50);
static_assert(FAKE_WAITER_LOCK_OFF == 0x58);
static_assert(FAKE_WAITER_WAKE_STATE_OFF == 0x60);
static_assert(FAKE_WAITER_WW_CTX_OFF == 0x68);
static_assert(FAKE_TASK_USAGE_OFF == 0x40);
static_assert(FAKE_TASK_PRIO_OFF == 0x84);
static_assert(FAKE_TASK_NORMAL_PRIO_OFF == 0x8c);
static_assert(FAKE_TASK_TASK_GROUP_OFF == 0x348);
static_assert(FAKE_TASK_PI_LOCK_OFF == 0x90c);
static_assert(FAKE_TASK_PI_WAITERS_OFF == 0x920);
static_assert(FAKE_TASK_PI_TOP_TASK_OFF == 0x930);
static_assert(FAKE_TASK_PI_BLOCKED_ON_OFF == 0x938);
static_assert(TASK_PID_OFF == 0x618);
static_assert(TASK_TGID_OFF == 0x61c);
static_assert(TASK_ATOMIC_FLAGS_OFF == 0x5d8);
static_assert(TASK_REAL_CRED_OFF == 0x818);
static_assert(TASK_CRED_OFF == 0x820);
static_assert(TASK_COMM_OFF == 0x830);
static_assert(TASK_TASKS_OFF == 0x550);
static_assert(TASK_THREAD_INFO_FLAGS_OFF == 0);
static_assert(TASK_SECCOMP_OFF == 0x8e8);
static_assert(CRED_UID_OFF == 8);
static_assert(CRED_SECUREBITS_OFF == 40);
static_assert(CRED_CAPS_OFF == 48);
static_assert(CRED_SECURITY_OFF == 128);
static_assert(SELINUX_CRED_BLOB_OFF == 0);
static_assert(SELINUX_CRED_OSID_OFF == 0);
static_assert(SELINUX_CRED_SID_OFF == 4);
static_assert(SECCOMP_MODE_OFF == 0);
static_assert(SECCOMP_FILTER_COUNT_OFF == 4);
static_assert(SECCOMP_FILTER_OFF == 8);
static_assert(TIF_SECCOMP_BIT == 11);
static_assert(PFA_NO_NEW_PRIVS_BIT == 0);
static_assert(STRUCT_PAGE_SIZE == 0x40);
static_assert(STRUCT_PAGE_COMPOUND_HEAD_OFF == 0x08);
static_assert(STRUCT_SLAB_CACHE_OFF == 0x08);
static_assert(STRUCT_PAGE_TYPE_OFF == 0x30);
static_assert(INIT_TASK == KIMAGE_TEXT_BASE + INIT_TASK_OFF);
static_assert(INIT_CRED == KIMAGE_TEXT_BASE + INIT_CRED_OFF);
static_assert(ROOT_TASK_GROUP == KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF);
static_assert(SELINUX_ENFORCING ==
              KIMAGE_TEXT_BASE + SELINUX_ENFORCING_OFF);
static_assert(SELINUX_BLOB_SIZES ==
              KIMAGE_TEXT_BASE + SELINUX_BLOB_SIZES_OFF);
static_assert(SECURITY_HOOK_HEADS ==
              KIMAGE_TEXT_BASE + SECURITY_HOOK_HEADS_OFF);
static_assert(SLIDE_NFULNL_LOGGER_IMAGE ==
              KIMAGE_TEXT_BASE + SLIDE_NFULNL_LOGGER_OFF);
static_assert(SLIDE_LOGGERS_0_1_IMAGE ==
              KIMAGE_TEXT_BASE + SLIDE_LOGGERS_0_1_OFF);
static_assert(SLIDE_RANDOM_BOOT_ID_DATA_IMAGE ==
              KIMAGE_TEXT_BASE + SLIDE_RANDOM_BOOT_ID_DATA_OFF);
static_assert(SLIDE_INIT_TASK_IMAGE == KIMAGE_TEXT_BASE + INIT_TASK_OFF);
static_assert(SLIDE_ROOT_TASK_GROUP_IMAGE ==
              KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF);
static_assert(SLIDE_SYSCTL_BOOTID_IMAGE ==
              KIMAGE_TEXT_BASE + SLIDE_SYSCTL_BOOTID_OFF);
static_assert(std::is_standard_layout_v<target::KernelImageAddress>);
static_assert(std::is_trivially_copyable_v<target::KernelImageAddress>);

int main() {
    const target::KernelImageAddress base(KIMAGE_TEXT_BASE);
    const auto target = base.checked_add(LOCK_OFF);
    assert(target && target->value() == KIMAGE_TEXT_BASE + LOCK_OFF);

    const target::KernelImageAddress near_end(
        std::numeric_limits<std::uintptr_t>::max() - 1);
    assert(!near_end.checked_add(2));
    return 0;
}
