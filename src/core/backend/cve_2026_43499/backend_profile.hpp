#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_BACKEND_PROFILE_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_BACKEND_PROFILE_HPP

/* Owner binding for the cve_2026_43499 profile transport (A2-5).
 *
 * The profile container frames a neutral Document; only the selected backend
 * knows how to bind it onto the frozen transport ABI. This unit is that bind
 * point: it filters the route sections the profile selected, validates every
 * (section, key) against the platform::abi + backend union fail-closed, merges
 * the platform View into profile::kernel_offsets and lands the backend values.
 * StepSet selection stays backend-private. */

#include "profile/document.hpp"
#include "backend/cve_2026_43499/backend_profile/model.hpp"
#include "profile/schema.hpp"

#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::backend_profile {
    /* Bind the Document's 43499 sections onto the frozen transport. Known route
     * sections that are not the selected route are dropped (exactly as the
     * legacy field walk skipped them). The release string is copied into
     * release_buf and out->uname_r points at it; the caller keeps release_buf
     * alive for the copy's lifetime (TargetProfile copies it). Returns Ok on
     * success, or a BindStatus naming the offending section/key. */
    profile::BindStatus bind(const profile::Document &document, uint8_t route,
                             profile::kernel_offsets *out, char *release_buf,
                             size_t release_buf_cap);

    /* The backend-private StepSet id carried by the backend section (0 when the
     * key is absent). Used by the composition root to select the Pipeline
     * instantiation before the binding install. */
    [[nodiscard]] uint16_t steps_from(const profile::Document &document) noexcept;
} // namespace ghostlock::backend::cve_2026_43499::backend_profile

#endif
