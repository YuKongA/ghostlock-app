/** @file profile/run_plan.cpp
 *  Resolving a document into a chain and an ordered list of steps.
 */
#include "profile/run_plan.hpp"

namespace ghostlock::profile {
    namespace {
        constexpr std::size_t kTokenCapacity = 32U;

    }  // namespace

    const contract::StepSetOrder *step_set_for_steps(const contract::StepId *steps,

                                                     std::size_t count) noexcept {
        if (steps == nullptr || count == 0U) {
            return nullptr;
        }
        for (std::size_t i = 0U; i < contract::kStepSetCount; ++i) {
            const contract::StepSetOrder &order = contract::kStepSetOrders[i];
            if (order.count != count) {
                continue;
            }
            bool same = true;
            for (std::size_t s = 0U; s < count; ++s) {
                if (order.steps[s] != steps[s]) {
                    same = false;
                }
            }
            if (same) {
                return &order;
            }
        }
        return nullptr;
    }

    RunPlanResult resolve_run_plan(const Document &document) noexcept {
        RunPlanResult result{};
        if (!document.is_open()) {
            result.verdict = PlanVerdict::DocumentInvalid;
            return result;
        }
        char backend[kTokenCapacity];
        char route[kTokenCapacity];
        char terminal[kTokenCapacity];
        if (document.root_text(std::string_view("backend"), backend, sizeof(backend)) !=

                DocumentVerdict::Ok ||
            document.root_text(std::string_view("terminal"), terminal, sizeof(terminal)) !=

                DocumentVerdict::Ok) {
            /* A document without a backend and a terminal asks for nothing. */
            result.verdict = PlanVerdict::SelectionMissing;
            return result;
        }
        const bool has_route =

            document.root_text(std::string_view("route"), route, sizeof(route)) ==

            DocumentVerdict::Ok;
        if (!has_route) {
            route[0U] = '\0';
        }

        /* Where the section for this backend would be, for the queue. */
        char section[192];
        std::size_t at = 0U;
        const auto append = [&section, &at](std::string_view text) noexcept {
            for (const char c : text) {
                if (at + 1U < sizeof(section)) {
                    section[at] = c;
                    ++at;
                }
            }
            section[at] = '\0';
        };
        append(std::string_view("backend."));
        append(std::string_view(backend));

        contract::StepId steps[kMaxRunSteps] = {};
        std::size_t step_count = 0U;
        const Document::StepQueueResult queue =
            document.step_queue(std::string_view(section), steps, kMaxRunSteps);
        /* There is no falling back: a document that carries no queue has not said
         * which steps to run, and "the table's default path" is not a substitute
         * for being told. What used to be a guess here is a refusal now. */
        if (queue.verdict != Document::QueueVerdict::Ok) {
            result.verdict = queue.verdict == Document::QueueVerdict::StepUnknown
                                 ? PlanVerdict::StepSetUnknown
                                 : PlanVerdict::QueueUnusable;
            return result;
        }
        step_count = queue.count;
        const contract::StepSetOrder *order = step_set_for_steps(steps, step_count);
        if (order == nullptr) {
            result.verdict = PlanVerdict::StepSetUnknown;
            return result;
        }
        const contract::ChainSpec *chain =
            contract::chain_for(std::string_view(backend), std::string_view(route),
                                std::string_view(terminal), std::string_view(order->step_set));
        if (chain == nullptr) {
            result.verdict = PlanVerdict::ChainUnknown;
            return result;
        }
        if (!chain->wired) {
            result.verdict = PlanVerdict::ChainNotWired;
            return result;
        }
        result.plan.chain = chain;
        for (std::size_t i = 0U; i < step_count; ++i) {
            result.plan.steps[i] = steps[i];
        }
        result.plan.step_count = step_count;
        result.verdict = PlanVerdict::Ok;
        return result;
    }
}  // namespace ghostlock::profile
