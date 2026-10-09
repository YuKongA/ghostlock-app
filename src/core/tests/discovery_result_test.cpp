/** @file tests/discovery_result_test.cpp
 *  The mapping from a found object to what a discovery reports.
 *
 *  Two rules carry it: a found object is reported in canonical form, and everything else is the
 *  failure the contract names -- a refusal, a walk that found nothing, and an object of zero all
 *  report that nothing was found rather than an address dressed as one.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/leak/discovery_result.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::canonical_address;
    using ghostlock::backend::cve_2026_43499::leak::kCanonicalTag;
    using ghostlock::backend::cve_2026_43499::leak::ObjectOutcome;
    using ghostlock::backend::cve_2026_43499::leak::ObjectVerdict;
    using ghostlock::backend::cve_2026_43499::leak::report_discovery;
    using ghostlock::backend::cve_2026_43499::leak::tagged_candidate;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void a_found_object_is_reported_canonical() {
        ObjectOutcome found;
        found.status = ObjectVerdict::Found;
        found.object = tagged_candidate(0x100000U, kCanonicalTag);
        const auto report = report_discovery(found);
        expect(report.ok, "a found object means the mechanism worked");
        expect(report.mm_struct == (found.object | (kCanonicalTag << 56U)),
               "and it is reported in canonical form");
        expect(canonical_address(0U) == (kCanonicalTag << 56U),
               "the restore puts the tag nibble where the kernel took it from");
        expect(canonical_address(tagged_candidate(0x2000U, 0U)) == canonical_address(0x2000U),
               "and it is what a lower-tagged address becomes too");
    }

    void nothing_else_is_reported_as_an_object() {
        ObjectOutcome refused;
        expect(!report_discovery(refused).ok, "a default outcome is a refusal");
        expect(report_discovery(refused).mm_struct == 0U, "and reports no address");
        ObjectOutcome walked;
        walked.status = ObjectVerdict::NotFound;
        walked.object = 0U;
        expect(!report_discovery(walked).ok, "a range walked with nothing answering reports that");
        ObjectOutcome claimed;
        claimed.status = ObjectVerdict::Found;
        claimed.object = 0U;
        expect(!report_discovery(claimed).ok,
               "and a found verdict with no address is the failure, not an object at zero");
        expect(report_discovery(claimed).mm_struct == 0U, "which is why the address is checked too");
    }
}  // namespace

int main() {
    std::printf("discovery result: what a found object reports\n");
    a_found_object_is_reported_canonical();
    std::printf("discovery result: what nothing reports\n");
    nothing_else_is_reported_as_an_object();
    if (g_failures != 0U) {
        std::printf("discovery result: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("discovery result: PASS (canonical when found, the failure otherwise)\n");
    return 0;
}
