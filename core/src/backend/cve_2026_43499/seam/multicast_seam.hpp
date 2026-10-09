#pragma once
/** @file backend/cve_2026_43499/seam/multicast_seam.hpp
 *  The first route behind the access seam: multicast, and only multicast.
 *
 *  Per the seam port design, this fills the seam's decisions and hands the window work to a
 *  window interface. The old route's body is what will implement that interface, ported line by
 *  line and with its dependency on the old global session replaced by the state passed in; until
 *  then a fake window drives the tests, so what is checked here is the part that is decided:
 *  which route this seam accepts, that it refuses the others by name, that bring_up keeps the
 *  target and tear_down gives it back, and that a landed word carries the address, the value and
 *  the offset unchanged.
 *
 *  Scope, stated rather than implied: one route. The switch in bring_up is the place the other
 *  two appear, and it is written so that adding them is adding a case rather than reshaping this.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/kernel_access.hpp"

namespace ghostlock::backend::cve_2026_43499::seam {
    /** The window, as this seam sees it: the work the old route body does. The real one writes
     *  through the PI window; the tests use one that records instead. */
    class Window {
    public:
        Window() noexcept = default;
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        virtual ~Window() noexcept = default;

        /** Open the window, do the write, close it and reclaim on every exit path. */
        [[nodiscard]] virtual bool write_word(std::uint64_t address, std::uint64_t value,
                                              std::uint64_t offset) noexcept = 0;
    };

    /** Multicast, behind the seam. */
    class MulticastSeam final : public AccessSeam {
    public:
        using AccessState = contract::KernelAccess::AccessState;

        /** The window is borrowed and must outlive this seam. */
        explicit MulticastSeam(Window &window) noexcept : window_(window) {}

        /** Only multicast, for now. Another route is refused by name rather than quietly served
         *  by this one, which is the failure a switch like this exists to prevent. */
        [[nodiscard]] bool bring_up(profile::RouteId route, const TargetState &target) noexcept override {
            if (route != profile::RouteId::MulticastWaiter) {
                return false;
            }
            if (up_) {
                return false;
            }
            target_ = target;
            up_ = true;
            return true;
        }

        [[nodiscard]] bool tear_down() noexcept override {
            if (!up_) {
                return false;
            }
            up_ = false;
            target_ = TargetState{};
            return true;
        }

        [[nodiscard]] bool land_word(std::uint64_t address, std::uint64_t value,
                                     std::uint64_t offset) noexcept override {
            if (!up_) {
                return false;
            }
            /* The facts the write lands on came from the search and the preparation and were
             *  handed to bring_up. A window body that finds them missing must not proceed, so
             *  the check is here rather than left to the window. */
            if (target_.page_base == 0U || target_.mm_struct == 0U) {
                return false;
            }
            return window_.write_word(address, value, offset);
        }

        /** Multicast writes and does not read; the object and the page are what it writes to. */
        [[nodiscard]] bool can_read() const noexcept override { return false; }

        /** Whether the route is up, for diagnostics and for tests. */
        [[nodiscard]] bool up() const noexcept { return up_; }
        /** The state the access would report, derived from this seam's own view. */
        [[nodiscard]] AccessState state() const noexcept {
            return up_ ? AccessState::Ready : AccessState::Declared;
        }

    private:
        Window &window_;
        TargetState target_{};
        bool up_ = false;
    };
}  // namespace ghostlock::backend::cve_2026_43499::seam
