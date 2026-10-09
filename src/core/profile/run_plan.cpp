/** @file profile/run_plan.cpp
 *  Resolving a document into a chain and the plan it carries.
 */
#include "profile/run_plan.hpp"

namespace ghostlock::profile {
    namespace {
        constexpr std::size_t kTokenCapacity = 32U;

        /** The retired key: what a document must not carry any more. Spelled here,
         *  once, because the reader that used to parse it is gone and this is the only
         *  place left that has to know its name. */
        constexpr const char *kRetiredQueueKey = "queue";

        /** The segment a backend's keys live under. */
        bool backend_section(char *out, std::size_t capacity, std::string_view backend) noexcept {
            if (out == nullptr || capacity == 0U || backend.empty()) {
                return false;
            }
            std::size_t at = 0U;
            const auto put = [out, capacity, &at](char c) noexcept {
                if (at + 1U < capacity) {
                    out[at] = c;
                    ++at;
                }
            };
            for (const char c : std::string_view("backend.")) {
                put(c);
            }
            for (const char c : backend) {
                put(c);
            }
            out[at] = '\0';
            return true;
        }
    }  // namespace

    RunPlanResult resolve_run_plan(const Document &document) noexcept {
        RunPlanResult result{};
        if (!document.is_open()) {
            result.verdict = RunPlanVerdict::DocumentInvalid;
            return result;
        }
        /* The retired carrier, refused on sight and before the selection is even read:
         *  a document that still carries a step queue is asking for something this
         *  build no longer reads, and ignoring it would run a plan nobody asked for.
         *  The scan is over every backend the catalogue declares, not only the one the
         *  selection names, because the rule is about the document and a document that
         *  carries a queue is refused whether or not it also names a chain. */
        for (std::size_t i = 0U; i < contract::kComponentCount; ++i) {
            if (contract::kComponents[i].kind != contract::ComponentKind::Backend) {
                continue;
            }
            char section[192];
            if (!backend_section(section, sizeof(section),
                                 std::string_view(contract::kComponents[i].token))) {
                result.verdict = RunPlanVerdict::DocumentInvalid;
                return result;
            }
            if (document.segment_has_key(std::string_view(section), std::string_view(kRetiredQueueKey))) {
                result.verdict = RunPlanVerdict::QueueRemoved;
                return result;
            }
        }

        /* One selection: the combination token. Everything about the chain follows
         *  from it, which is why the document does not name a backend, a route or a
         *  terminal separately -- there is nowhere for a second selection to live. */
        char combination[kTokenCapacity];
        if (document.root_text(std::string_view("combination"), combination,

                               sizeof(combination)) != DocumentVerdict::Ok) {
            result.verdict = RunPlanVerdict::SelectionMissing;
            return result;
        }
        const contract::ChainSpec *chain = contract::chain_of(std::string_view(combination));
        if (chain == nullptr) {
            /* The token is echoed in the refusal rather than guessed at: an unknown
             *  selection is the one case where saying what was asked for matters. */
            result.verdict = RunPlanVerdict::ChainUnknown;
            return result;
        }
        if (!chain->wired) {
            result.verdict = RunPlanVerdict::ChainNotWired;
            return result;
        }
        /* The plan: a script the document carries, named for the terminal the chain
         *  hands off to. The delivery reader owns the ceiling and the digest. */
        PlanDelivery delivered;
        const PlanVerdict delivery = read_plan_delivery(document, std::string_view(chain->backend),
                                                        std::string_view(chain->terminal), delivered);
        switch (delivery) {
            case PlanVerdict::Ok:
                break;
            case PlanVerdict::PlanMissing:
                result.verdict = RunPlanVerdict::PlanMissing;
                return result;
            case PlanVerdict::BudgetExceeded:
                result.verdict = RunPlanVerdict::PlanTooLarge;
                return result;
            case PlanVerdict::DocumentInvalid:
                result.verdict = RunPlanVerdict::DocumentInvalid;
                return result;
        }
        result.plan.chain = chain;
        result.plan.plan = delivered;
        result.verdict = RunPlanVerdict::Ok;
        return result;
    }
}  // namespace ghostlock::profile
