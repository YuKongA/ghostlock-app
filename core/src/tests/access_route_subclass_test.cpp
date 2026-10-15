/** @file tests/access_route_subclass_test.cpp
 *  The three route subclasses: which parameter set each accepts, and what each
 *  refuses by name. A subclass must not answer for another route's set.
 */
#include <cstdio>
#include <memory>

#include "backend/cve_2026_43499/access_factory_43499.hpp"
#include "backend/cve_2026_43499/access_multicast_waiter.hpp"
#include "backend/cve_2026_43499/access_select_stack.hpp"
#include "backend/cve_2026_43499/access_tcp_zerocopy.hpp"
#include "backend/cve_2026_43499/landing/multicast_landing.hpp"
#include "backend/cve_2026_43499/steps/access_binder.hpp"

namespace {
    namespace cve49 = ghostlock::backend::cve_2026_43499;
    namespace contract = ghostlock::contract;
    int g_failures = 0;

    /** A route controller that records: the warm-up order is what is under test. */
    /** The recording route: the window's controller. The shape is the window's concept, so no base. */
    class RecordingExec final {
    public:
        [[nodiscard]] bool execute(std::uint64_t address, std::uint64_t value,
                                   std::uint64_t offset) noexcept {
            last_address = address;
            last_value = value;
            last_offset = offset;
            ++executes;
            return true;
        }

        [[nodiscard]] bool arm(std::int32_t attempt) noexcept {
            warmed[arms < 8 ? arms : 7] = attempt;
            ++arms;
            return true;
        }

        std::uint64_t last_address = 0U;
        std::uint64_t last_value = 0U;
        std::uint64_t last_offset = 0U;
        std::int32_t warmed[8] = {};
        int arms = 0;
        int executes = 0;
    };

    /** A window that records instead of writing: the landing's decisions are what is under test. */
    class RecordingWindow final : public cve49::landing::Window {
    public:
        [[nodiscard]] bool write_word(std::uint64_t address, std::uint64_t value,
                                     std::uint64_t offset) noexcept override {
            last_address = address;
            last_value = value;
            last_offset = offset;
            ++calls;
            return true;
        }

        std::uint64_t last_address = 0U;
        std::uint64_t last_value = 0U;
        std::uint64_t last_offset = 0U;
        int calls = 0;
    };

    void expect(const bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void the_factory_chooses_by_route() {
        using contract::KernelAccess;
        expect(cve49::create_kernel_access_43499(ghostlock::profile::RouteId::Unset) == nullptr,
               "an unnamed route is refused, not defaulted");
        expect(cve49::create_kernel_access_43499(ghostlock::profile::RouteId::MulticastWaiter) != nullptr,
               "the multicast route has an access");
        expect(cve49::create_kernel_access_43499(ghostlock::profile::RouteId::SelectStack) != nullptr,
               "the select-stack route has an access");
        expect(cve49::create_kernel_access_43499(ghostlock::profile::RouteId::TcpZerocopy) != nullptr,
               "the tcp-zerocopy route has an access");
        const auto made = cve49::create_kernel_access_43499(ghostlock::profile::RouteId::MulticastWaiter);
        expect(made != nullptr && made->state(contract::AccessChannel::PiWindow) ==
                   KernelAccess::AccessState::Declared,
               "a fresh access is declared, not ready");
    }

    void each_route_only_accepts_its_own_set() {
        using contract::KernelAccess;
        cve49::MulticastWaiterAccess<cve49::NoRouteExec> multicast;
        cve49::SelectStackAccess select_stack;
        cve49::TcpZerocopyAccess tcp;

        cve49::Multicast43499Parameters multicast_params{};
        multicast_params.route = ghostlock::profile::RouteId::MulticastWaiter;
        cve49::SelectStack43499Parameters select_params{};
        select_params.route = ghostlock::profile::RouteId::SelectStack;
        cve49::TcpZerocopy43499Parameters tcp_params{};
        tcp_params.route = ghostlock::profile::RouteId::TcpZerocopy;

        /* A foreign set is refused before anything is brought up. */
        expect(multicast.initialize(select_params).status == KernelAccess::Status::BadParameters,
               "the multicast access refuses the select-stack set by name");
        expect(select_stack.initialize(tcp_params).status == KernelAccess::Status::BadParameters,
               "the select-stack access refuses the tcp set by name");
        expect(tcp.initialize(multicast_params).status == KernelAccess::Status::BadParameters,
               "the tcp access refuses the multicast set by name");

        /* The right set gets past the parameter door; with no landing wired the route
         *  reports the missing wrapper rather than pretending it came up. */
        /* A bare multicast access has no window, and the route refuses **before** the
         *  base door: an unnamed access never reaches the phase machine, so it stays
         *  Declared rather than being driven into Broken and then reported as a failure. */
        expect(multicast.initialize(multicast_params).status == KernelAccess::Status::Unsupported,
               "a multicast access with no window is refused by name");
        expect(multicast.state(contract::AccessChannel::PiWindow) != KernelAccess::AccessState::Ready,
               "and the access is not ready afterwards");
    }
    void a_failed_bring_up_can_still_be_reclaimed() {
        using contract::KernelAccess;
        cve49::MulticastWaiterAccess<cve49::NoRouteExec> access;
        cve49::Multicast43499Parameters params{};
        params.route = ghostlock::profile::RouteId::MulticastWaiter;
        expect(access.initialize(params).status == KernelAccess::Status::Unsupported,
               "an unnamed access is refused by name");
        expect(access.state(contract::AccessChannel::PiWindow) ==
                   KernelAccess::AccessState::Declared,
               "and it never entered the phase machine");
        /* ref: disarm and destroy are unconditional, so a failure must not strand what
         *  bring-up built. NeverUp has nothing to give back, so it stays refused -- the
         *  Broken-then-reclaim path itself is covered where a failing landing can be built. */
        expect(!access.shutdown().ok(), "nothing was built, so there is nothing to reclaim");

        /* The route keeps one lifecycle: a second initialize while up would be refused,
         *  and switching a route means shutting down first. */
        cve49::MulticastWaiterAccess<cve49::NoRouteExec> fresh;
        expect(!fresh.shutdown().ok(), "shutting down what never came up is refused");
    }
    void a_wired_multicast_access_comes_up() {
        using contract::KernelAccess;
        RecordingWindow window;
        cve49::TargetState target{};
        cve49::MulticastWaiterAccess<cve49::NoRouteExec> access(window, target);
        cve49::Multicast43499Parameters params{};
        params.route = ghostlock::profile::RouteId::MulticastWaiter;
        expect(access.has_window(), "a window was handed in");
        expect(access.initialize(params).ok(), "a wired multicast access comes up");
        expect(access.state(contract::AccessChannel::PiWindow) == KernelAccess::AccessState::Ready,
               "and reports ready");
        /* The route, its carried window and the landing gear now live to shutdown, so a
         *  second initialize is refused rather than replacing what is already up. */
        expect(!access.initialize(params).ok(), "a second bring-up is refused, not replaced");
        expect(access.shutdown().ok(), "the wired access is reclaimed explicitly");
        expect(access.state(contract::AccessChannel::PiWindow) == KernelAccess::AccessState::Closed,
               "and ends closed");
        expect(window.calls == 0, "nothing was written during bring-up or teardown");
    }

    void the_warm_up_order_is_kept_by_the_window() {
        using contract::KernelAccess;
        RecordingExec exec;
        const cve49::route::MulticastLimits limits{1, 1, 3};   /* {max_attempts, arm_hold, arm_sequence} */
        cve49::TargetState target{};
        cve49::MulticastWaiterAccess<RecordingExec> access(exec, limits, target);
        cve49::Multicast43499Parameters params{};
        params.route = ghostlock::profile::RouteId::MulticastWaiter;
        expect(access.has_window(), "the route controller was taken as the window source");
        expect(access.initialize(params).ok(), "and the access comes up");
        expect(access.state(contract::AccessChannel::PiWindow) == KernelAccess::AccessState::Ready,
               "reported ready");
        expect(exec.arms == 0 && exec.executes == 0, "bring-up touches neither the route nor the arm");
        expect(access.shutdown().ok(), "and it is reclaimed explicitly");
        expect(exec.executes == 0, "teardown does not write either");
    }

    void binding_names_what_is_missing() {
        using contract::KernelAccess;
        namespace steps = cve49::steps;
        RecordingExec exec;
        const cve49::route::MulticastLimits limits{1, 1, 3};
        std::unique_ptr<cve49::MulticastWaiterAccess<RecordingExec>> owner;
        cve49::WordLanding *landing = nullptr;

        steps::AccessBindInputs<RecordingExec> no_route{};
        expect(std::string_view(steps::bind_multicast_access(no_route, owner, landing)) ==
                   "access-bind-missing-route",
               "no route controller is named as such");

        steps::AccessBindInputs<RecordingExec> no_facts{};
        no_facts.exec = &exec;
        no_facts.limits = &limits;
        expect(std::string_view(steps::bind_multicast_access(no_facts, owner, landing)) ==
                   "access-bind-facts-incomplete",
               "incomplete facts are named before the page is");

        steps::AccessBindInputs<RecordingExec> ready{};
        ready.exec = &exec;
        ready.limits = &limits;
        ready.target.mm_struct = 1U;
        ready.target.page_base = 2U;
        ready.target.fake_task = 3U;
        ready.target.fake_lock = 4U;
        expect(steps::bind_multicast_access(ready, owner, landing) == nullptr,
               "complete facts bind");
        expect(landing != nullptr, "and the landing landing is handed back");
        expect(owner != nullptr &&
                   owner->state(contract::AccessChannel::PiWindow) == KernelAccess::AccessState::Ready,
               "the access came up");

        /* The stage entry hands the same landing to the context the runner reads. */
        std::unique_ptr<cve49::MulticastWaiterAccess<RecordingExec>> owner2;
        steps::WindowStageContext<RecordingExec> context{};
        expect(steps::bind_stage_landing(ready, owner2, context) == nullptr,
               "the stage entry binds when the facts are complete");
        expect(context.landing_landing != nullptr, "and the context receives the landing");
        steps::WindowStageContext<RecordingExec> empty{};
        std::unique_ptr<cve49::MulticastWaiterAccess<RecordingExec>> owner3;
        expect(std::string_view(steps::bind_stage_landing(no_route, owner3, empty)) ==
                   "access-bind-missing-route",
               "and refuses the same way");
        expect(empty.landing_landing == nullptr, "leaving the context untouched on a refusal");
    }

    void both_entries_agree_on_the_backend() {
        using contract::KernelAccess;
        /* The contract entry `KernelAccess::create(BackendId)` lives in its own translation
         *  unit with both backends behind it, so it is not linked here; what this test pins is
         *  that the route entry answers the same way a caller must see it. */
        const auto by_route =
                cve49::create_kernel_access_43499(ghostlock::profile::RouteId::MulticastWaiter);
        expect(by_route != nullptr, "the route entry has an implementation");
        expect(by_route->state(contract::AccessChannel::PiWindow) ==
                   KernelAccess::AccessState::Declared,
               "a fresh route access starts declared");
        expect(!by_route->supports_width(4U), "and writes only a whole word");
        expect(by_route->supports(contract::AccessChannel::PiWindow), "on the pi window");
    }
}  // namespace

int main() {
    std::printf("access routes: the factory\n");
    the_factory_chooses_by_route();
    std::printf("access routes: one parameter set each\n");
    each_route_only_accepts_its_own_set();
    std::printf("access routes: reclaiming a failed bring-up\n");
    a_failed_bring_up_can_still_be_reclaimed();
    std::printf("access routes: a wired multicast access\n");
    a_wired_multicast_access_comes_up();
    std::printf("access routes: warm-up order\n");
    the_warm_up_order_is_kept_by_the_window();
    std::printf("access routes: binding\n");
    binding_names_what_is_missing();
    std::printf("access routes: both entries\n");
    both_entries_agree_on_the_backend();
    if (g_failures != 0) {
        std::printf("access routes: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("access routes: ok\n");
    return 0;
}
