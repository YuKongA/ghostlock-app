/* 43499 active-profile seam definitions (A2-4-3).
 *
 * The neutral offset accessors (profile/runtime_struct_offsets.h) and the
 * 43499 slide accessors (backend/.../backend_profile/accessors.hpp) read the
 * resolved profile through this seam; the two definitions below are the only
 * place that binds them to the 43499 session state. Kept out of
 * cve_2026_43499_state.hpp so the widely included state header stays a pure
 * state/layout header. */

#include "backend/cve_2026_43499_state.hpp"
#include "profile/runtime_struct_offsets.h"

#include <cstdint>

namespace ghostlock::profile {
    [[nodiscard]] const TargetProfile *active_profile() noexcept {
        return &ghostlock::backend::cve43499_state(
                        ghostlock::session::g_exploit_session)
                        .profile;
    }

    [[nodiscard]] uintptr_t active_data_alias(uintptr_t image_offset) noexcept {
        return ghostlock::backend::cve43499_state(
                       ghostlock::session::g_exploit_session)
                .addresses.data_alias(image_offset);
    }
} // namespace ghostlock::profile
