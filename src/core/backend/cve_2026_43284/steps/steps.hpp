#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_STEPS_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_STEPS_HPP

/* CVE-2026-43284 step-set vocabulary -- SKELETON.
 *
 * Step sets are backend-owned (ADR-0004 R18/R21). This header deliberately does
 * not include pipeline/: the StepSetKind binding lives in
 * cve_2026_43284_backend.hpp, because a backend must not include pipeline
 * headers (ADR-0004 R1). PageCacheWrite is 3 in the shared catalogue. */

#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::steps {

    struct PageCacheWriteSteps final {
        static constexpr std::string_view name = "pagecache_write";
        static constexpr std::uint16_t id = 3;
        /* TODO(B5-6): static StageResult run(CoreSession&, ...) once the chain
         * lands; the signature is intentionally not frozen yet. */
    };

} // namespace ghostlock::backend::cve_2026_43284::steps

#endif
