/** @file tests/window_steps_test.cpp
 *  43499 窗口阶段的证据：三个名字、尝试/让位/修复/链轮的节奏，以及四条具名失败。
 *
 *  「尝试一次」是注入的：假的尝试按脚本回答（第几次赢、是否先失败），所以这套节奏在主机上可测。
 */
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43499/steps/window_steps.hpp"

using namespace ghostlock;

namespace {
    namespace windows = backend::cve_2026_43499::steps;

    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++g_failures; }
    }

    /** 假世界：第 N 次尝试才赢；记录让位与修复次数。 */
    struct World final {
        std::uint32_t win_on = 1U;
        std::uint32_t tries = 0U;
        std::uint32_t settles = 0U;
        std::uint32_t settles_us = 0U;
        std::uint32_t repairs = 0U;
    };

    World *g_world = nullptr;

    bool world_attempt(void *context) noexcept {
        auto *world = static_cast<World *>(context);
        if (world == nullptr) { return false; }
        ++world->tries;
        return world->tries >= world->win_on;
    }

    void world_settle(std::uint32_t us) noexcept {
        if (g_world != nullptr) { ++g_world->settles; g_world->settles_us += us; }
    }

    bool world_repair(void *context) noexcept {
        if (context != nullptr) { ++static_cast<World *>(context)->repairs; }
        return true;
    }

    void names_map_both_ways() {
        const char *expected[] = {"w1", "w2", "w3"};
        expect(windows::kWindowStepCount == 3U, "three stages are declared");
        for (std::size_t i = 0U; i < windows::kWindowStepCount; ++i) {
            windows::WindowStep step{};
            expect(windows::window_step_by_name(expected[i], step) &&
                           std::strcmp(windows::window_step_name(step), expected[i]) == 0,
                   "a stage name round-trips");
        }
        windows::WindowStep unused{};
        expect(!windows::window_step_by_name("w4", unused), "an unknown name is not a stage");
    }

    void a_stage_wins_on_the_third_attempt() {
        World world{};
        world.win_on = 3U;
        g_world = &world;
        windows::WindowStageRequest request{};
        request.attempts = 5U;
        request.settle_us = 1000U;
        request.attempt = &world_attempt;
        request.settle = &world_settle;
        request.context = &world;
        expect(windows::run_window_stage(windows::WindowStep::W2, request) == nullptr,
               "a stage can win late");
        expect(world.tries == 3U, "it tried exactly until it won");
        expect(world.settles == 2U && world.settles_us == 2000U, "it settled between attempts");
        g_world = nullptr;
    }

    void w1_repairs_the_scratch_once_and_tries_again() {
        World world{};
        world.win_on = 100U;   /* 第一轮永远不赢 */
        g_world = &world;
        windows::WindowStageRequest request{};
        request.attempts = 2U;
        request.settle_us = 500U;
        request.scratch_repair_attempts = 1U;
        request.attempt = &world_attempt;
        request.settle = &world_settle;
        request.repair_scratch = &world_repair;
        request.context = &world;
        expect(std::strcmp(windows::run_window_stage(windows::WindowStep::W1, request),
                           "step-window-not-won") == 0,
               "a stage that never wins says so");
        expect(world.repairs == 1U, "w1 repaired the scratch once");
        expect(world.tries == 4U, "and tried again after the repair");
        g_world = nullptr;
    }

    void w3_chains_its_rounds() {
        World world{};
        /* w3 的语义是「每一轮都要赢」：让它每次尝试都赢，看它走满轮数。 */
        world.win_on = 1U;
        g_world = &world;
        windows::WindowStageRequest request{};
        request.attempts = 1U;
        request.chain_rounds = 3U;
        request.settle_us = 7U;
        request.attempt = &world_attempt;
        request.settle = &world_settle;
        request.context = &world;
        expect(windows::run_window_stage(windows::WindowStep::W3, request) == nullptr,
               "w3 runs its rounds");
        expect(world.tries == 3U, "one attempt per round");
        expect(world.settles == 2U && world.settles_us == 14U, "and settles between rounds");
        g_world = nullptr;
    }

    void the_refusals_are_named() {
        windows::WindowStageRequest bare{};
        expect(std::strcmp(windows::run_window_stage(windows::WindowStep::W1, bare),
                           "step-attempt-missing") == 0,
               "no attempt hook is a named refusal");
        World world{};
        windows::WindowStageRequest request{};
        request.attempt = &world_attempt;
        request.context = &world;
        expect(std::strcmp(windows::run_window_stage(windows::WindowStep::W1, request),
                           "step-attempts-zero") == 0,
               "zero attempts is a named refusal");
        request.attempts = 1U;
        request.scratch_repair_attempts = 1U;
        expect(std::strcmp(windows::run_window_stage(windows::WindowStep::W1, request),
                           "step-repair-missing") == 0,
               "repairs without a repair hook is a named refusal");
        request.scratch_repair_attempts = 0U;
        request.chain_rounds = 0U;
        expect(std::strcmp(windows::run_window_stage(windows::WindowStep::W3, request),
                           "step-rounds-zero") == 0,
               "zero rounds is a named refusal");
    }
}  // namespace

int main() {
    std::printf("window steps: names");
    names_map_both_ways();
    std::printf("window steps: the rhythm");
    a_stage_wins_on_the_third_attempt();
    w1_repairs_the_scratch_once_and_tries_again();
    w3_chains_its_rounds();
    std::printf("window steps: named failures");
    the_refusals_are_named();
    if (g_failures != 0U) {
        std::printf("window steps: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("window steps: PASS (three stages, one rhythm, named failures)\n");
    return 0;
}
