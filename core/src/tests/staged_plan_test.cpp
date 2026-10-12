/** @file tests/staged_plan_test.cpp
 *  What a script run recorded, before anything runs.
 *
 *  A6-2 had no test; this is that test, and it is written against the contract
 *  the header states rather than against the implementation: forwarded calls keep
 *  their surface row, their form and their arguments verbatim; bounds are named
 *  and the first refusal is the one kept; two runs of one script serialize to the
 *  same bytes, which is what makes a recording comparable at all.
 *
 *  Sections: 1. what was forwarded comes back; 2. bounds and refusals;
 *  3. the image is canonical.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "contract/script_surface.hpp"
#include "script/staged_plan.hpp"

namespace {
    using ghostlock::contract::SurfaceForm;
    using ghostlock::script::ArgView;
    using ghostlock::script::CallView;
    using ghostlock::script::Handle;
    using ghostlock::script::kMaxStagedArgs;
    using ghostlock::script::kMaxStagedEntries;
    using ghostlock::script::kStagedArenaBytes;
    using ghostlock::script::StagedPlan;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    ArgView arg_u64(std::uint64_t value) {
        ArgView arg;
        arg.kind = ghostlock::contract::ArgKind::U64;
        arg.u64 = value;
        return arg;
    }

    ArgView arg_str(std::string_view text) {
        ArgView arg;
        arg.kind = ghostlock::contract::ArgKind::Str;
        arg.str = text;
        return arg;
    }

    ArgView arg_bool(bool value) {
        ArgView arg;
        arg.kind = ghostlock::contract::ArgKind::Bool;
        arg.boolean = value;
        return arg;
    }

    ArgView arg_handle(std::uint64_t kind, std::uint64_t slot, std::uint64_t generation) {
        ArgView arg;
        arg.kind = ghostlock::contract::ArgKind::Handle;
        /* A handle's parts are narrower than the numbers a caller has in hand. */
        arg.handle = Handle{static_cast<decltype(Handle::kind)>(kind),
                            static_cast<decltype(Handle::slot)>(slot),
                            static_cast<decltype(Handle::generation)>(generation)};
        return arg;
    }

    CallView call(std::size_t surface_index, SurfaceForm form, const ArgView *args,
                  std::size_t arg_count) {
        CallView view;
        view.surface_index = surface_index;
        view.form = form;
        view.name = "test";
        view.args = args;
        view.arg_count = arg_count;
        return view;
    }

    /* ---- 1: what was forwarded comes back ---- */
    void what_was_forwarded_comes_back() {
        StagedPlan plan;
        const ArgView first[] = {arg_u64(7U)};
        const ArgView second[] = {arg_str("selinux_cred_seccomp")};
        const ArgView third[] = {arg_bool(true), arg_handle(2U, 5U, 9U)};
        expect(plan.record(call(0U, SurfaceForm::Call, first, 1U)), "a call is recorded");
        expect(plan.record(call(3U, SurfaceForm::HandleMethod, second, 1U)),
               "and another with a string");
        expect(plan.record(call(1U, SurfaceForm::Value, third, 2U)),
               "and one with a boolean and a handle");
        expect(plan.ok() && plan.size() == 3U, "all three are there and nothing is refused");
        expect(plan.at(0U).surface_index == 0U && plan.at(1U).surface_index == 3U,
               "each keeps the surface row it was forwarded from");
        expect(plan.at(2U).form == SurfaceForm::Value, "and the form it was reached by");
        expect(plan.at(0U).arg_count == 1U && plan.at(2U).arg_count == 2U,
               "and how many arguments it carried");
        expect(plan.at(0U).args[0].u64 == 7U, "a number comes back as itself");
        expect(plan.string_of(plan.at(1U).args[0]) == std::string_view("selinux_cred_seccomp"),
               "a string comes back as the text it was");
        expect(plan.at(2U).args[0].boolean, "a boolean keeps its value");
        expect(plan.at(2U).args[1].handle.slot == 5U && plan.at(2U).args[1].handle.generation == 9U,
               "and a handle keeps all three of its parts");
        /* A string is a span, not a C string: bytes are kept as given. */
        const char raw[] = {'a', '\0', 'b'};
        StagedPlan with_nul;
        const ArgView embedded[] = {arg_str(std::string_view(raw, sizeof(raw)))};
        expect(with_nul.record(call(0U, SurfaceForm::Call, embedded, 1U)), "a string is recorded");
        const std::string_view kept = with_nul.string_of(with_nul.at(0U).args[0]);
        expect(kept.size() == 3U && kept[1U] == '\0',
               "and an embedded NUL is a byte like any other, not an end");
        expect(with_nul.arena_size() == 3U, "the arena holds exactly those bytes");
    }

    /* ---- 2: bounds and refusals ---- */
    void bounds_are_named() {
        StagedPlan plan;
        const ArgView one[] = {arg_u64(1U)};
        for (std::size_t i = 0U; i < kMaxStagedEntries; ++i) {
            expect(plan.record(call(i, SurfaceForm::Call, one, 1U)), "records up to the bound");
        }
        expect(!plan.record(call(0U, SurfaceForm::Call, one, 1U)),
               "and refuses the one past it");
        expect(std::string_view(plan.refusal_name()) == std::string_view("budget-exceeded"),
               "naming the bound it hit");
        expect(plan.size() == kMaxStagedEntries, "without dropping an entry it did take");
        /* The first refusal is the one kept: a later one never overwrites it. */
        expect(!plan.refuse("something-else") &&
                   std::string_view(plan.refusal_name()) == std::string_view("budget-exceeded"),
               "a later refusal does not replace the first");
        expect(!plan.record(call(0U, SurfaceForm::Call, one, 1U)),
               "and a refused plan stays refused");

        StagedPlan wide;
        ArgView too_many[kMaxStagedArgs + 1U];
        for (std::size_t i = 0U; i <= kMaxStagedArgs; ++i) {
            too_many[i] = arg_u64(i);
        }
        expect(!wide.record(call(0U, SurfaceForm::HandleMethod, too_many, kMaxStagedArgs + 1U)),
               "a call with more arguments than an entry holds is refused");
        expect(wide.size() == 0U, "and nothing of it is kept");
        expect(std::string_view(wide.refusal_name()) == std::string_view("budget-exceeded"),
               "by name");

        StagedPlan small;
        char big[kStagedArenaBytes + 16U];
        std::memset(big, 'x', sizeof(big));
        const ArgView spills[] = {arg_str(std::string_view(big, sizeof(big)))};
        expect(!small.record(call(0U, SurfaceForm::Call, spills, 1U)),
               "strings that do not fit the arena are refused");
        expect(small.arena_size() == 0U, "and the arena is left as it was");
    }

    /* ---- 3: the image is canonical ---- */
    void two_runs_serialize_the_same() {
        const ArgView args[] = {arg_str("multicast_waiter"), arg_u64(0x2000U)};
        StagedPlan first;
        StagedPlan second;
        for (StagedPlan *plan : {&first, &second}) {
            (void) plan->record(call(0U, SurfaceForm::Call, args, 2U));
            (void) plan->record(call(4U, SurfaceForm::HandleMethod, args + 1U, 1U));
        }
        std::uint8_t image_a[512U] = {};
        std::uint8_t image_b[512U] = {};
        const std::size_t wrote_a = first.serialize(image_a, sizeof(image_a));
        const std::size_t wrote_b = second.serialize(image_b, sizeof(image_b));
        expect(wrote_a != 0U && wrote_a == wrote_b, "both runs wrote the same number of bytes");
        expect(std::memcmp(image_a, image_b, wrote_a) == 0,
               "and the bytes are identical, which is what makes a recording comparable");
        StagedPlan other;
        (void) other.record(call(1U, SurfaceForm::Call, args, 2U));
        std::uint8_t image_c[512U] = {};
        const std::size_t wrote_c = other.serialize(image_c, sizeof(image_c));
        expect(wrote_c != 0U && (wrote_c != wrote_a || std::memcmp(image_a, image_c, wrote_a) != 0),
               "a different call gives a different image");
        expect(first.serialize(image_a, 4U) == 0U,
               "a buffer too small is refused rather than half-filled");
        first.reset();
        expect(first.ok() && first.size() == 0U && first.arena_size() == 0U,
               "resetting drops the entries and any refusal");
    }
}  // namespace

int main() {
    std::printf("staged plan: what was forwarded\n");
    what_was_forwarded_comes_back();
    std::printf("staged plan: bounds and refusals\n");
    bounds_are_named();
    std::printf("staged plan: the image\n");
    two_runs_serialize_the_same();
    if (g_failures != 0U) {
        std::printf("staged plan: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("staged plan: PASS (verbatim + named bounds + canonical image)\n");
    return 0;
}
