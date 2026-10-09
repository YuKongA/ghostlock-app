#pragma once
/** @file profile/plan_delivery.hpp
 *  The plan a document delivers, and why it could not be read.
 *
 *  The plan is a payload: the family already carries bytes with an explicit length,
 *  checks that the declared size matches them, and has a naming rule with a
 *  required suffix, so a plan is a payload named after the terminal it drives
 *  with the plan kind -- not a second carrier beside the family.
 *
 *  Two judgements belong to this reader and not to the family: the size ceiling,
 *  which is the loader's bound rather than a wire check, and the digest, which the
 *  family only checks the shape of and which is recomputed here. A digest that
 *  does not match is not reported under a second name: from the plan point of view
 *  a payload that is not what was declared is a missing payload, which is the
 *  single-name discipline the execution side already follows.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/script_surface.hpp"
#include "profile/glkv3_document.hpp"

namespace ghostlock::profile {
    /** Why a plan could not be delivered. Names follow the single table. */
    enum class PlanVerdict : unsigned char {
        Ok = 0,
        /** Absent, or not what it declared itself to be: one name by design. */
        PlanMissing = 1,
        /** Larger than a script may be: the loader's own bound. */
        BudgetExceeded = 2,
        /** The document itself could not be read. */
        DocumentInvalid = 3,
    };

    [[nodiscard]] constexpr const char *plan_verdict_name(PlanVerdict verdict) noexcept {
        switch (verdict) {
            case PlanVerdict::Ok: return "ok";
            case PlanVerdict::PlanMissing: return "plan-missing";
            case PlanVerdict::BudgetExceeded: return "budget-exceeded";
            case PlanVerdict::DocumentInvalid: return "document-invalid";
        }
        return "unknown";
    }

    /** What was delivered: the bytes stay a view into the document's buffer. */
    struct PlanDelivery final {
        const std::uint8_t *script = nullptr;
        std::size_t size = 0U;
        /** The digest the document declared, terminated here, for the record. */
        char declared_sha256[65] = {};
    };

    /** The payload name a plan is delivered under, for one terminal: the terminal
     *  token the family accepts, the underscore the family requires, and plan. */
    inline constexpr const char *kPlanSuffix = "_plan";

    /** Read the plan for a terminal out of a document. The name is built from the
     *  terminal rather than passed in, so there is exactly one spelling of it. */
    [[nodiscard]] PlanVerdict read_plan_delivery(const Document &document,

                                                 std::string_view backend,

                                                 std::string_view terminal,

                                                 PlanDelivery &out) noexcept;
}  // namespace ghostlock::profile
