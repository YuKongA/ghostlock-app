/** @file profile/plan_delivery.cpp
 *  Reading the plan payload: the family's checks, plus the ceiling and the digest.
 */
#include "profile/plan_delivery.hpp"

#include "support/sha256.hpp"

namespace ghostlock::profile {
    namespace {
        constexpr std::size_t kPlanNameCapacity = 64U;

        std::size_t build_name(std::string_view terminal, char *out, std::size_t capacity) noexcept {
            std::size_t at = 0U;
            const auto put = [&out, &at, capacity](char c) noexcept {
                if (at + 1U < capacity) {
                    out[at] = c;
                    ++at;
                }
            };
            for (const char c : terminal) {
                put(c);
            }
            for (const char *c = kPlanSuffix; *c != '\0'; ++c) {
                put(*c);
            }
            out[at] = '\0';
            return at;
        }
    }  // namespace

    PlanVerdict read_plan_delivery(const Document &document, std::string_view backend,
                                   std::string_view terminal, PlanDelivery &out) noexcept {
        out = PlanDelivery{};
        if (!document.is_open() || backend.empty() || terminal.empty()) {
            return PlanVerdict::DocumentInvalid;
        }
        char name[kPlanNameCapacity];
        (void) build_name(terminal, name, sizeof(name));
        PayloadEntry entry;
        const DocumentVerdict verdict =
            document.payload(backend, std::string_view(name), entry);
        if (verdict != DocumentVerdict::Ok) {
            /* Absent, or not the shape the family requires: either way there is no
             * plan here to run. */
            return PlanVerdict::PlanMissing;
        }
        /* The ceiling is this reader's judgement: the family checks that the size
         * key matches the bytes, and nothing more. An empty payload never reaches
         * here -- the family folds a declared size of zero into missing, the same
         * way the execution side does -- so there is no second branch for it. */
        if (entry.size > contract::kScriptSizeCeiling) {
            return PlanVerdict::BudgetExceeded;
        }
        /* The digest the family checked the shape of, recomputed over the bytes. */
        char computed[support::kSha256HexBytes];
        support::sha256_hex(support::sha256(entry.bytes, entry.size), computed);
        for (std::size_t i = 0U; i < support::kSha256HexBytes; ++i) {
            if (computed[i] != entry.sha256[i]) {
                /* One name by design: not what it declared itself to be means the
                 * plan is not here. */
                return PlanVerdict::PlanMissing;
            }
        }
        out.script = entry.bytes;
        out.size = entry.size;
        for (std::size_t i = 0U; i < support::kSha256HexBytes; ++i) {
            out.declared_sha256[i] = entry.sha256[i];
        }
        return PlanVerdict::Ok;
    }
}  // namespace ghostlock::profile
