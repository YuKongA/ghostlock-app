/** @file tests/a2_test.cpp
 *  A2 evidence: the window reclaims on every exit path, the window prohibitions
 *  are accounted for, the profile read path names a missing key instead of
 *  defaulting, the neutral support pieces behave, and the minimal PI race walks
 *  its four states without allocation or indirect dispatch.
 *
 *  Build and run: make -C src check
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "platform/target_profile.hpp"
#include "backend/cve_2026_43499/race/pi_race.hpp"
#include "backend/cve_2026_43499/race/pi_window.hpp"
#include "support/neutrals.hpp"

namespace {
    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    using ghostlock::contract::ExitPath;
    using ghostlock::memory::KeyLookup;
    using ghostlock::memory::KeyValue;
    using ghostlock::memory::KeyView;
    using ghostlock::memory::ProfileRejection;
    using ghostlock::memory::profile_rejection_name;
    using ghostlock::memory::read_key;
    using ghostlock::backend::cve_2026_43499::race::PiRace;
    using ghostlock::backend::cve_2026_43499::race::RaceOutcome;
    using ghostlock::backend::cve_2026_43499::race::WindowGuard;

    /* 1. every exit path reclaims exactly once. */
    void exit_paths() {
        for (std::size_t i = 0; i < ghostlock::contract::kExitPathCount; ++i) {
            const ExitPath path = static_cast<ExitPath>(i);
            std::size_t observed = 0;
            {
                WindowGuard guard;
                guard.observe_reclaim(&observed);
                guard.enter(path);
                if (path == ExitPath::Failure) {
                    guard.reclaim(); /* explicit failure reclaim */
                } else if (path == ExitPath::Retry) {
                    guard.reclaim(); /* first attempt reclaimed; the second is left to the destructor */
                    guard.enter(path);
                }
                /* cancel and early-return deliberately leave the reclaim to the destructor */
            }
            expect(observed >= 1, "every exit path must reclaim at least once (the destructor is the net)");
            if (path == ExitPath::Retry) {
                expect(observed == 2, "the retry path reclaims both attempts");
            }
            std::printf("exit-path %s: reclaims=%zu ok\n", ghostlock::contract::exit_path_name(path), observed);
        }
        {
            WindowGuard guard;
            guard.enter(ExitPath::Success);
            guard.reclaim();
            guard.reclaim(); /* idempotent */
            expect(guard.reclaim_count() == 1, "reclaim must be idempotent");
        }
    }

    /* 2. the prohibition accounting refuses a dirty window. */
    void prohibitions() {
        WindowGuard guard;
        guard.enter(ExitPath::Success);
        expect(guard.clean_run(), "a fresh window must be clean");
        guard.note_allocation();
        guard.note_log();
        guard.note_indirect_call();
        guard.note_lua_call();
        expect(!guard.clean_run(), "any reported violation must make the window dirty");
        expect(guard.violations().total() == 4, "all four violation kinds are counted");
        std::printf("prohibitions: %zu rows, violations counted=%u\n",
                    ghostlock::backend::cve_2026_43499::race::kWindowProhibitionCount, guard.violations().total());
    }

    /* 3. the profile read path: named failures, never a default. */
    void profile_read_path() {
        const KeyView empty{};
        const KeyLookup missing_source = read_key(empty, "kernel_major");
        expect(!missing_source.ok && missing_source.reason == ProfileRejection::EmptySource,
               "an empty source must be refused by name");

        const KeyValue one[] = {{"kernel_major", 5U}};
        const KeyView view{one, 1};
        const KeyLookup found = read_key(view, "kernel_major");
        expect(found.ok && found.value == 5U, "a present key must be returned");
        const KeyLookup absent = read_key(view, "kernel_minor");
        expect(!absent.ok && absent.reason == ProfileRejection::MissingKey, "a missing key must be refused");
        expect(absent.key == "kernel_minor", "the refusal must name the missing key");
        expect(absent.value == 0U, "a refused lookup must not fabricate a value");

        const KeyValue twice[] = {{"kernel_major", 5U}, {"kernel_major", 6U}};
        const KeyView dup_view{twice, 2};
        const KeyLookup dup = read_key(dup_view, "kernel_major");
        expect(!dup.ok && dup.reason == ProfileRejection::DuplicateKey, "a duplicate key must be refused");
        std::printf("profile-read: missing=%s key=%.*s duplicate=%s\n", profile_rejection_name(absent.reason),
                    static_cast<int>(absent.key.size()), absent.key.data(), profile_rejection_name(dup.reason));
    }

    /* 4. the neutral support pieces. */
    void neutrals() {
        int runs = 0;
        {
            auto guard = ghostlock::support::on_scope_exit([&runs]() noexcept { ++runs; });
            expect(!ghostlock::support::succeeded(ghostlock::support::Status::Failed), "Status::Failed is a failure");
            (void) guard;
        }
        expect(runs == 1, "the scope-exit action must run exactly once");
        {
            auto guard = ghostlock::support::on_scope_exit([&runs]() noexcept { ++runs; });
            guard.dismiss();
        }
        expect(runs == 1, "a dismissed guard must not run its action");
        std::printf("neutrals: scope-exit runs once, dismiss is honoured\n");
    }

    /* 5. the minimal PI race: four states, no window violation. */
    void pi_race_states() {
        PiRace<> race;
        expect(race.outcome() == RaceOutcome::NotPrepared, "a fresh race is not prepared");
        std::uint32_t chain = 0;
        std::uint32_t target = 0;
        WindowGuard guard;
        guard.enter(ExitPath::Success);
        race.prepare(&chain, &target);
        /* No waiter is parked on the chain here, so the kernel moves none and the attempt is
         *  refused. That is the proven shape: the owner locks the target, drives the requeue,
         *  and the count of waiters it moved is the evidence. The success path needs a waiter,
         *  and it was proven on the device -- owned=0 requeued=1 released=0 waiter=0 -- in the
         *  entry that runs one. */
        expect(race.run(guard) == RaceOutcome::Refused,
               "an open window without a waiter is refused, because the kernel moved none");
        race.disarm();
        expect(race.outcome() == RaceOutcome::Disarmed, "disarm ends the attempt");
        race.destroy();
        expect(race.outcome() == RaceOutcome::Destroyed, "destroy ends the attempt");
        expect(guard.clean_run(), "the race must not report a window violation");
        std::printf("pi-race: not-prepared -> running -> disarmed -> destroyed, window clean\n");

        PiRace<> refused;
        refused.prepare(nullptr, nullptr);
        WindowGuard closed;
        expect(refused.outcome() == RaceOutcome::NotPrepared, "prepare with no words is not prepared");
        expect(refused.run(closed) == RaceOutcome::NotPrepared, "an unprepared race cannot run");
    }
}  // namespace

int main() {
    exit_paths();
    prohibitions();
    profile_read_path();
    neutrals();
    pi_race_states();
    if (failures == 0) {
        std::printf("a2: PASS (window reclaim per exit path + prohibitions + profile read path + neutrals + pi race)\n");
        return 0;
    }
    std::printf("a2: FAIL (%d)\n", failures);
    return 1;
}
