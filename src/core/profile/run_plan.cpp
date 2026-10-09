/** @file profile/run_plan.cpp
 *  Resolving a document into a chain and an ordered list of steps.
 */
#include "profile/run_plan.hpp"

namespace ghostlock::profile {
    namespace {
        constexpr std::size_t kTokenCapacity = 32U;

        /** The chain rows that match a selection, ignoring the path. */
        const contract::ChainSpec *chain_by_selection(std::string_view backend,
                                                       std::string_view route,

                                                       std::string_view terminal,

                                                       bool prefer_path_matching_terminal) noexcept {
            for (std::size_t i = 0U; i < contract::kChainCount; ++i) {
                const contract::ChainSpec &row = contract::kChainCatalog[i];
                if (std::string_view(row.backend) != backend ||
                    std::string_view(row.route) != route ||
                    std::string_view(row.terminal) != terminal) {
                    continue;
                }
                if (prefer_path_matching_terminal && std::string_view(row.path) != terminal) {
                    continue;
                }
                return &row;
            }
            return nullptr;
        }
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
        const bool has_queue = queue.verdict == Document::QueueVerdict::Ok;
        if (queue.verdict != Document::QueueVerdict::Ok &&
            queue.verdict != Document::QueueVerdict::QueueMissing) {
            /* The queue is there and unusable: a refusal, not a fallback, and a step
             * the catalogue does not know is named as such rather than folded into
             * "there is no queue". */
            result.verdict = queue.verdict == Document::QueueVerdict::StepUnknown
                                 ? PlanVerdict::StepSetUnknown
                                 : PlanVerdict::QueueUnusable;
            return result;
        }


        const contract::ChainSpec *chain = nullptr;
        if (has_queue) {
            step_count = queue.count;
            const contract::StepSetOrder *order = step_set_for_steps(steps, step_count);
            if (order == nullptr) {
                result.verdict = PlanVerdict::StepSetUnknown;
                return result;
            }
            chain = contract::chain_for(std::string_view(backend), std::string_view(route),

                                        std::string_view(terminal),

                                        std::string_view(order->step_set));
        } else {
            /* TEMPORARY (see the header): the document carries no queue, so the
             * chain table's default path for this terminal stands in for it. */
            chain = chain_by_selection(std::string_view(backend), std::string_view(route),

                                       std::string_view(terminal), true);
            if (chain == nullptr) {
                result.verdict = PlanVerdict::ChainUnknown;
                return result;
            }
            const contract::StepSetOrder *order =

                contract::step_set_order(std::string_view(chain->step_set));
            if (order == nullptr || order->count == 0U || order->count > kMaxRunSteps) {
                result.verdict = PlanVerdict::StepSetUnknown;
                return result;
            }
            for (std::size_t i = 0U; i < order->count; ++i) {
                steps[i] = order->steps[i];
            }
            step_count = order->count;
        }
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
        result.plan.from_fallback = !has_queue;
        result.verdict = PlanVerdict::Ok;
        return result;
    }
}  // namespace ghostlock::profile
