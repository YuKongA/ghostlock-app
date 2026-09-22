#include "kernel/target.h"

using namespace ghostlock;

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>


static_assert(ghostlock::kernel::KIMAGE_TEXT_BASE == 0xffffffc080000000ULL);
static_assert(ghostlock::kernel::MTK_VADDR_BASE == 0xffffffc000000000ULL);
static_assert(ghostlock::kernel::P0_PAGE_OFFSET == 0xffffff8000000000ULL);
static_assert(ghostlock::kernel::P0_PHYS_OFFSET == 0x80000000ULL);
static_assert(ghostlock::kernel::KERNELSNITCH_IDENTITY_START == 0xffffff8000000000ULL);
static_assert(ghostlock::kernel::KERNELSNITCH_IDENTITY_END == 0xffffff8c00000000ULL);
static_assert(ghostlock::kernel::DIRECT_MAP_BASE == 0xffffff8000000000ULL);
static_assert(ghostlock::kernel::DIRECT_MAP_END == 0xffffff9000000000ULL);
static_assert(ghostlock::kernel::VMEMMAP_START == 0xfffffffe00000000ULL);
static_assert(ghostlock::kernel::LOCK_OFF == 0x0e80);
static_assert(ghostlock::kernel::W0_OFF == 0x1180);
static_assert(ghostlock::kernel::FOPS_OFF == 0x0f80);
static_assert(ghostlock::kernel::RIGHT_OFF == 0x1240);
static_assert(ghostlock::kernel::LEFT_OFF == 0x1260);
static_assert(ghostlock::kernel::FAKE_TASK_OFF == 0x1280);
static_assert(ghostlock::kernel::CRED_COPY_OFF == 0x1080);
static_assert(ghostlock::kernel::TCP_FAKE_TASK_OFF == 0x5800);
static_assert(ghostlock::kernel::TCP_CRED_COPY_OFF == 0x6800);

/* Compatibility vectors for target.h values deliberately left profile-owned.
 * They prove CPP01 did not silently reinterpret device/kernel data as C++
 * compile-time authority. */
static_assert(ghostlock::kernel::P0_KERNEL_PHYS_LOAD == 0xa8000000ULL);
static_assert(ghostlock::kernel::QC_GKI_6_12_PHYS_LOAD == 0xc7800000ULL);
static_assert(ghostlock::kernel::XRING_KERNEL_PHYS_LOAD == 0x80200000ULL);
static_assert(ghostlock::kernel::INIT_TASK_OFF == 0x0211e280ULL);
static_assert(ghostlock::kernel::INIT_CRED_OFF == 0x02130748ULL);
static_assert(ghostlock::kernel::ROOT_TASK_GROUP_OFF == 0x02317580ULL);
static_assert(ghostlock::kernel::SELINUX_ENFORCING_OFF == 0x02358ee0ULL);
static_assert(ghostlock::kernel::SELINUX_BLOB_SIZES_OFF == 0x0167b5d0ULL);
static_assert(ghostlock::kernel::SECURITY_HOOK_HEADS_OFF == 0);
static_assert(ghostlock::kernel::SLIDE_NFULNL_LOGGER_OFF == 0x02112260ULL);
static_assert(ghostlock::kernel::SLIDE_LOGGERS_0_1_OFF == 0x021121b0ULL);
static_assert(ghostlock::kernel::SLIDE_RANDOM_BOOT_ID_DATA_OFF == 0x02379ed8ULL);
static_assert(ghostlock::kernel::SLIDE_SYSCTL_BOOTID_OFF == 0x02379ed8ULL);
static_assert(ghostlock::kernel::PSELECT_WAITER_WORD_SHIFT == -2);
static_assert(ghostlock::kernel::FAKE_WAITER_TREE_PRIO_OFF == 0x18);
static_assert(ghostlock::kernel::FAKE_WAITER_TREE_DEADLINE_OFF == 0x20);
static_assert(ghostlock::kernel::FAKE_WAITER_PI_TREE_ENTRY_OFF == 0x28);
static_assert(ghostlock::kernel::FAKE_WAITER_PI_TREE_PRIO_OFF == 0x40);
static_assert(ghostlock::kernel::FAKE_WAITER_PI_TREE_DEADLINE_OFF == 0x48);
static_assert(ghostlock::kernel::FAKE_WAITER_TASK_OFF == 0x50);
static_assert(ghostlock::kernel::FAKE_WAITER_LOCK_OFF == 0x58);
static_assert(ghostlock::kernel::FAKE_WAITER_WAKE_STATE_OFF == 0x60);
static_assert(ghostlock::kernel::FAKE_WAITER_WW_CTX_OFF == 0x68);
static_assert(ghostlock::kernel::FAKE_TASK_USAGE_OFF == 0x40);
static_assert(ghostlock::kernel::FAKE_TASK_PRIO_OFF == 0x84);
static_assert(ghostlock::kernel::FAKE_TASK_NORMAL_PRIO_OFF == 0x8c);
static_assert(ghostlock::kernel::FAKE_TASK_TASK_GROUP_OFF == 0x348);
static_assert(ghostlock::kernel::FAKE_TASK_PI_LOCK_OFF == 0x90c);
static_assert(ghostlock::kernel::FAKE_TASK_PI_WAITERS_OFF == 0x920);
static_assert(ghostlock::kernel::FAKE_TASK_PI_TOP_TASK_OFF == 0x930);
static_assert(ghostlock::kernel::FAKE_TASK_PI_BLOCKED_ON_OFF == 0x938);
static_assert(ghostlock::kernel::TASK_PID_OFF == 0x618);
static_assert(ghostlock::kernel::TASK_TGID_OFF == 0x61c);
static_assert(ghostlock::kernel::TASK_ATOMIC_FLAGS_OFF == 0x5d8);
static_assert(ghostlock::kernel::TASK_REAL_CRED_OFF == 0x818);
static_assert(ghostlock::kernel::TASK_CRED_OFF == 0x820);
static_assert(ghostlock::kernel::TASK_COMM_OFF == 0x830);
static_assert(ghostlock::kernel::TASK_TASKS_OFF == 0x550);
static_assert(ghostlock::kernel::TASK_THREAD_INFO_FLAGS_OFF == 0);
static_assert(ghostlock::kernel::TASK_SECCOMP_OFF == 0x8e8);
static_assert(ghostlock::kernel::CRED_UID_OFF == 8);
static_assert(ghostlock::kernel::CRED_SECUREBITS_OFF == 40);
static_assert(ghostlock::kernel::CRED_CAPS_OFF == 48);
static_assert(ghostlock::kernel::CRED_SECURITY_OFF == 128);
static_assert(ghostlock::kernel::SELINUX_CRED_BLOB_OFF == 0);
static_assert(ghostlock::kernel::SELINUX_CRED_OSID_OFF == 0);
static_assert(ghostlock::kernel::SELINUX_CRED_SID_OFF == 4);
static_assert(ghostlock::kernel::SECCOMP_MODE_OFF == 0);
static_assert(ghostlock::kernel::SECCOMP_FILTER_COUNT_OFF == 4);
static_assert(ghostlock::kernel::SECCOMP_FILTER_OFF == 8);
static_assert(ghostlock::kernel::TIF_SECCOMP_BIT == 11);
static_assert(ghostlock::kernel::PFA_NO_NEW_PRIVS_BIT == 0);
static_assert(ghostlock::kernel::STRUCT_PAGE_SIZE == 0x40);
static_assert(ghostlock::kernel::STRUCT_PAGE_COMPOUND_HEAD_OFF == 0x08);
static_assert(ghostlock::kernel::STRUCT_SLAB_CACHE_OFF == 0x08);
static_assert(ghostlock::kernel::STRUCT_PAGE_TYPE_OFF == 0x30);
static_assert(ghostlock::kernel::INIT_TASK == ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::INIT_TASK_OFF);
static_assert(ghostlock::kernel::INIT_CRED == ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::INIT_CRED_OFF);
static_assert(
    ghostlock::kernel::ROOT_TASK_GROUP == ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::ROOT_TASK_GROUP_OFF);
static_assert(ghostlock::kernel::SELINUX_ENFORCING ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::SELINUX_ENFORCING_OFF);
static_assert(ghostlock::kernel::SELINUX_BLOB_SIZES ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::SELINUX_BLOB_SIZES_OFF);
static_assert(ghostlock::kernel::SECURITY_HOOK_HEADS ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::SECURITY_HOOK_HEADS_OFF);
static_assert(ghostlock::kernel::SLIDE_NFULNL_LOGGER_IMAGE ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::SLIDE_NFULNL_LOGGER_OFF);
static_assert(ghostlock::kernel::SLIDE_LOGGERS_0_1_IMAGE ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::SLIDE_LOGGERS_0_1_OFF);
static_assert(ghostlock::kernel::SLIDE_RANDOM_BOOT_ID_DATA_IMAGE ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::SLIDE_RANDOM_BOOT_ID_DATA_OFF);
static_assert(
    ghostlock::kernel::SLIDE_INIT_TASK_IMAGE == ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::INIT_TASK_OFF);
static_assert(ghostlock::kernel::SLIDE_ROOT_TASK_GROUP_IMAGE ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::ROOT_TASK_GROUP_OFF);
static_assert(ghostlock::kernel::SLIDE_SYSCTL_BOOTID_IMAGE ==
              ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::SLIDE_SYSCTL_BOOTID_OFF);
static_assert(std::is_standard_layout_v<target::KernelAddress<target::ImageAddressDomain> >);
static_assert(std::is_trivially_copyable_v<target::KernelAddress<target::ImageAddressDomain> >);

int32_t main() {
    const target::KernelAddress<target::ImageAddressDomain> base(ghostlock::kernel::KIMAGE_TEXT_BASE);
    const auto target = base.checked_add(ghostlock::kernel::LOCK_OFF);
    assert(target && target->value() == ghostlock::kernel::KIMAGE_TEXT_BASE + ghostlock::kernel::LOCK_OFF);

    const target::KernelAddress<target::ImageAddressDomain> near_end(
        std::numeric_limits<std::uintptr_t>::max() - 1);
    assert(!near_end.checked_add(2));
    return 0;
}
