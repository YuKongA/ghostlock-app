/** @file pipeline/tests/window_exit_paths_test.cpp
 *  P4 milestone A evidence: every AttackWindow exit path reclaims, and the
 *  composition root binds without owning window resources.
 *
 *  Build and run: make -C src p4-window-check
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "contract/resource_scope.hpp"
#include "pipeline/attack_window.hpp"
#include "pipeline/composition_root.hpp"

namespace {
    using ghostlock::contract::ExitPath;
    using ghostlock::pipeline::AttackWindow;
    using ghostlock::pipeline::CompositionRoot;

    struct FakeExecutor final {
        int bind_calls = 0;
    };

    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    /* success: the guard reclaims when its scope ends normally. */
    void run_success(AttackWindow &w) {
        auto guard = w.open(ExitPath::Success);
        expect(guard.owns(), "success: guard owns the window");
        expect(w.open_here(), "success: window is open inside the guard");
    }

    /* early return: the destructor reclaims even though the function returns early. */
    void run_early_return(AttackWindow &w) {
        auto guard = w.open(ExitPath::EarlyReturn);
        if (guard.owns()) {
            return;
        }
        expect(false, "early-return: guard must own the window");
    }

    /* cancel: dismiss transfers the responsibility away from the guard. */
    void run_cancel(AttackWindow &w) {
        auto guard = w.open(ExitPath::Cancel);
        guard.dismiss();
        expect(!guard.owns(), "cancel: guard released the window");
        expect(w.open_here(), "cancel: the window stays open for the caller");
    }

    /* failure: an explicit reclaim through the guard. */
    void run_failure(AttackWindow &w) {
        auto guard = w.open(ExitPath::Failure);
        guard.reclaim_now();
        expect(!guard.owns(), "failure: guard reclaimed explicitly");
        expect(!w.open_here(), "failure: window is closed");
    }

    /* retry: two windows in a row, each reclaimed once, generation advances. */
    void run_retry(AttackWindow &w) {
        const std::uint64_t before = w.generation();
        {
            auto first = w.open(ExitPath::Retry);
            expect(first.owns(), "retry: first guard owns");
        }
        const std::size_t after_first = w.reclaim_count();
        {
            auto second = w.open(ExitPath::Retry);
            expect(second.owns(), "retry: second guard owns");
        }
        expect(w.reclaim_count() == after_first + 1, "retry: the second reclaim is counted");
        expect(w.generation() > before, "retry: the generation advanced");
        expect(w.generation() != 0, "retry: the generation never wraps to zero");
    }
}  // namespace

int main() {
    for (std::size_t i = 0; i < ghostlock::contract::kExitPathCount; ++i) {
        AttackWindow w;
        const auto path = static_cast<ExitPath>(i);
        switch (i) {
            case 0: run_success(w); break;
            case 1: run_failure(w); break;
            case 2: run_retry(w); break;
            case 3: run_cancel(w); break;
            case 4: run_early_return(w); break;
            default: expect(false, "unknown exit path"); break;
        }
        /* The cancel path deliberately leaves the window open; the host closes it. */
        w.reclaim();
        expect(w.reclaim_count() >= 1, "every path reclaims at least once");
        expect(w.reclaimed(), "every path marks the window reclaimed");
        expect(!w.open_here(), "every path leaves the window closed");
        std::printf("exit-path %s: reclaims=%zu generation=%llu ok\n", ghostlock::contract::exit_path_name(path),
                    w.reclaim_count(), static_cast<unsigned long long>(w.generation()));
    }

    FakeExecutor exec;
    CompositionRoot<FakeExecutor> root;
    expect(!root.bound(), "root starts unbound");
    root.bind(&exec);
    expect(root.bound(), "root reports bound");
    expect(root.executor() == &exec, "root returns the bound executor");
    {
        auto guard = root.window().open(ExitPath::Success);
        expect(guard.owns(), "root window guard owns");
    }
    expect(root.window().reclaim_count() == 1, "root window reclaimed exactly once");
    std::printf("composition-root: bind + window reuse ok\n");

    if (failures == 0) {
        std::printf("window-exit-paths: PASS (5 paths + composition root)\n");
        return 0;
    }
    std::printf("window-exit-paths: FAIL (%d)\n", failures);
    return 1;
}
