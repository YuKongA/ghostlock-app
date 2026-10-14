#pragma once
/** @file backend/cve_2026_43499/access_select_stack.hpp
 *  The SelectStack route's kernel access.
 *
 *  One subclass per route (2026-10-12 ruling): the shared logic stays in
 *  KernelAccess43499 and this class adds only what the route alone knows --
 *  which parameter set initialize accepts, and which upper-layer wrapper does
 *  the route's multi-step work.  The wrapper call is a seam on purpose: an
 *  omission must stay visible as a named refusal, never as a silent success.
 */
#include "backend/cve_2026_43499/access_params.hpp"
#include "backend/cve_2026_43499/kernel_access.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    class SelectStackAccess final : public KernelAccess43499 {
    public:
        static constexpr profile::RouteId kRoute = profile::RouteId::SelectStack;

        /** The fd_set geometry is read from SelectStack43499Parameters. */
        [[nodiscard]] contract::KernelAccess::AccessResult<bool> initialize(
            const AccessMiscParameters &parameters) noexcept override {
            using BoolResult = contract::KernelAccess::AccessResult<bool>;
            /* The route only accepts its own parameter set; another route's set is
             *  refused by name rather than reinterpreted. */
            if (dynamic_cast<const SelectStack43499Parameters *>(&parameters) == nullptr) {
                return BoolResult::failed(AccessStatus::BadParameters);
            }
            /* super: the shared preconditions and the phase transition. */
            const BoolResult base = KernelAccess43499::initialize(parameters);
            if (!base.ok()) {
                return base;
            }
            /* Specialization: this route's multi-step work runs in the upper-layer
             *  wrapper, not in the write path. Until that wrapper is wired the step
             *  reports itself missing -- by name. */
            if (!arm_route()) {
                return BoolResult::failed(AccessStatus::Unsupported);
            }
            return BoolResult::ok(true);
        }

    protected:
        /** The upper-layer wrapper for this route's multi-step work (socket, stamp,
         *  setsockopt).  Not write logic: it must not be reached from land_word().
         *  @return true when the route's gear is up; false keeps the omission named. */
        /* 非虚（2026-10-14 零告警批次）：本类是 final ⇒ clang 的 -Wunnecessary-virtual-specifier
         *  指出"final 类里不可能被覆盖的虚函数"是死码。将来真要让某个路线接上层 wrapper，
         *  先按当时的裁定把本类改成非 final，再决定是否恢复虚函数。 */
        [[nodiscard]] bool arm_route() noexcept { return false; }
    };
}  // namespace ghostlock::backend::cve_2026_43499
