/* M2 queue wire test: the canonical selection surface end to end over real
 * GLKv3 bytes (design doc sections 4.5 / 5.0 / 5-Q2 / 5-Q3, batch M2).
 *
 *   - a queue document normalizes to the same combination the equivalent token
     document produces (the same-shape corpus, item 5 of the M2 brief);
 *   - every declaration-time rule fails closed AND prints its named
 *     `plan_error reason=` token (stderr is captured and asserted, so the
 *     diagnostic text is a guarded contract, not a comment);
 *   - an Array on an undeclared key stays rejected (declaration-driven). */

#include "contract/identity.hpp"
#include "contract/step_catalog.hpp"
#include "profile/glkv3.hpp"
#include "profile/glkv3_parse.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using ghostlock::contract::BackendKind;
using ghostlock::contract::CombinationKind;
using ghostlock::contract::CombinationSpec;
using ghostlock::profile::glkv3::Document;
using ghostlock::profile::glkv3::Entry;
using ghostlock::profile::glkv3::MapMember;
using ghostlock::profile::glkv3::Section;
using ghostlock::profile::glkv3::Value;
using ghostlock::profile::glkv3::WireType;

namespace {
    Value str_value(std::string_view text) {
        Value value;
        value.type = WireType::Str;
        value.bytes = text;
        return value;
    }

    Value bool_value(bool raw) {
        Value value;
        value.type = WireType::Bool;
        value.bool_value = raw;
        return value;
    }

    Value map_value(std::vector<MapMember> members) {
        Value value;
        value.type = WireType::Map;
        value.members = std::move(members);
        return value;
    }

    Value array_value(std::vector<Value> elements) {
        Value value;
        value.type = WireType::Array;
        value.elements = std::move(elements);
        return value;
    }

    MapMember member(std::string_view key, Value value) {
        MapMember out;
        out.key = key;
        out.type = value.type;
        out.uint_value = value.uint_value;
        out.int_value = value.int_value;
        out.bool_value = value.bool_value;
        out.bytes = value.bytes;
        return out;
    }

    Value step_element(std::string_view token) {
        return map_value({member("step", str_value(token))});
    }

    Value seam_element(std::string_view stage) {
        return map_value({member("seam", str_value("plugin")),
                          member("stage", str_value(stage))});
    }

    /* Documents carry the root selection exactly as the App emits it. */
    Document make_queue_doc(std::string_view backend, std::string_view terminal,
                            std::string_view root_route, bool has_root_route,
                            std::vector<Value> elements, std::string_view section_route,
                            bool has_section_route, bool experimental,
                            bool has_experimental) {
        Document doc;
        doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
        doc.has_release = true;
        doc.release = "6.6.77-queue-test";
        doc.has_terminal = true;
        doc.terminal = terminal;
        doc.has_backend = true;
        doc.backend = backend;
        if (has_root_route) {
            doc.has_route = true;
            doc.route = root_route;
        }
        Section &target = doc.append_section(backend == "cve_2026_43499"
                                                     ? "backend.cve_2026_43499"
                                                     : "backend.cve_2026_43284");
        if (has_section_route) {
            target.entries.push_back(Entry{"route", str_value(section_route)});
        }
        if (has_experimental) {
            target.entries.push_back(Entry{"experimental", bool_value(experimental)});
        }
        target.entries.push_back(Entry{"queue", array_value(std::move(elements))});
        return doc;
    }

    int frame(const Document &doc, ghostlock::profile::Document &out) {
        const std::string encoded = ghostlock::profile::glkv3::encode(doc);
        if (encoded.empty()) return -1;
        return ghostlock::profile::frame_v3(encoded, &out);
    }

    /* Capture stderr so the named diagnostic is asserted, not assumed. */
    std::string capture_stderr(const Document &doc, int &status) {
        char path[] = "/tmp/glk_queue_diag_XXXXXX";
        const int fd = mkstemp(path);
        assert(fd >= 0);
        const int saved = dup(STDERR_FILENO);
        assert(saved >= 0);
        const int redirected = dup2(fd, STDERR_FILENO);
        assert(redirected >= 0);
        ghostlock::profile::Document framed;
        status = frame(doc, framed);
        (void)fflush(stderr);
        assert(dup2(saved, STDERR_FILENO) >= 0);
        close(saved);
        lseek(fd, 0, SEEK_SET);
        std::string text;
        char buffer[512];
        ssize_t got = 0;
        while ((got = read(fd, buffer, sizeof(buffer))) > 0) {
            text.append(buffer, static_cast<std::size_t>(got));
        }
        close(fd);
        unlink(path);
        return text;
    }

    const CombinationSpec &spec_of(CombinationKind kind) {
        const CombinationSpec *found = ghostlock::contract::combination_spec(kind);
        assert(found != nullptr);
        return *found;
    }

    std::vector<Value> steps_of(CombinationKind kind) {
        std::vector<Value> elements;
        const ghostlock::contract::StepSetAlias *alias =
                ghostlock::contract::step_set_alias(spec_of(kind).steps);
        assert(alias != nullptr);
        for (const ghostlock::contract::StepId id : alias->steps) {
            const ghostlock::contract::StepSpec *step = ghostlock::contract::step_spec(id);
            assert(step != nullptr);
            elements.push_back(step_element(step->token));
        }
        return elements;
    }

    int failures = 0;

    void expect_reject(const char *what, const Document &doc, const char *reason) {
        int status = 0;
        const std::string text = capture_stderr(doc, status);
        const std::string needle = std::string("plan_error reason=") + reason;
        if (status == 0 || text.find(needle) == std::string::npos) {
            std::fprintf(stderr, "queue_wire_test: %s: expected reject with %s, status=%d\n",
                         what, needle.c_str(), status);
            ++failures;
        }
    }

    void expect_combination(const char *what, const Document &doc, CombinationKind kind) {
        ghostlock::profile::Document framed;
        if (frame(doc, framed) != 0) {
            std::fprintf(stderr, "queue_wire_test: %s: expected a plan, got a reject\n", what);
            ++failures;
            return;
        }
        if (framed.combination != static_cast<uint8_t>(kind)) {
            std::fprintf(stderr, "queue_wire_test: %s: combination=%u expected=%u\n", what,
                         static_cast<unsigned>(framed.combination),
                         static_cast<unsigned>(kind));
            ++failures;
        }
        /* M5-migrated coverage: these two were asserted on the removed TOKEN
         * path - the queue path must derive the same route and terminal as the
         * catalogue entry it normalises onto. */
        if (framed.middleware != static_cast<uint16_t>(spec_of(kind).route) ||
            framed.terminal_token !=
                    ghostlock::contract::terminal_token_name(spec_of(kind).terminal)) {
            std::fprintf(stderr,
                         "queue_wire_test: %s: route/terminal drifted from the catalogue\n",
                         what);
            ++failures;
        }
        /* M5 guard: the plan is carried IN MEMORY (combination/middleware/
         * terminal_token) and the normalised document must NOT grow a `steps`
         * entry - writing the preset token back would put a removed syntax on the
         * wire and make the document un-reparsable (a re-parse refuses it with
         * reason=token-form-removed). */
        if (framed.find_value("backend.cve_2026_43499", "steps") != nullptr ||
            framed.find_value("backend.cve_2026_43284", "steps") != nullptr) {
            std::fprintf(stderr,
                         "queue_wire_test: %s: the normalised document must not carry "
                         "a steps entry\n",
                         what);
            ++failures;
        }
    }
} // namespace

int main() {
    /* ---- D4 (handoff): a handoff that does not belong to the matched
     * combination is refused BY NAME, and an unknown or absent terminal keeps
     * its own reason - fail-closed preserved, never silently remapped. ---- */
    {
        const CombinationSpec &spec = spec_of(CombinationKind::McastRootchild);
        const bool routed = spec.route != ghostlock::profile::RouteKind::None;
        const std::string_view route_token = ghostlock::contract::route_name(spec.route);
        const Document accepted = make_queue_doc(
                ghostlock::contract::backend_token_name(spec.backend),
                ghostlock::contract::terminal_token_name(spec.terminal), route_token, routed,
                steps_of(CombinationKind::McastRootchild), route_token, routed, false, false);
        expect_combination("terminal baseline", accepted, CombinationKind::McastRootchild);

        Document mismatched = accepted;
        mismatched.terminal = ghostlock::contract::terminal_token_name(
                ghostlock::contract::TerminalKind::UmhForward);
        expect_reject("terminal disagrees with the combination", mismatched,
                      "terminal-disagrees-with-combination");

        Document unknown = accepted;
        unknown.terminal = "not_a_terminal";
        expect_reject("unknown terminal", unknown, "terminal-unknown");
    }

    /* ---- same-shape corpus: queue == token for every verified preset ---- */
    {
        const CombinationKind wired[] = {
                CombinationKind::McastRootchild, CombinationKind::PselectRootchild,
                CombinationKind::TcpRootchild,   CombinationKind::McastShizuku,
                CombinationKind::PselectShizuku, CombinationKind::TcpShizuku,
                CombinationKind::Umh};
        for (const CombinationKind kind : wired) {
            const CombinationSpec &spec = spec_of(kind);
            const bool routed = spec.route != ghostlock::profile::RouteKind::None;
            const std::string_view backend_token =
                    ghostlock::contract::backend_token_name(spec.backend);
            const std::string_view route_token =
                    ghostlock::contract::route_name(spec.route);
            const std::string_view terminal_token =
                    ghostlock::contract::terminal_token_name(spec.terminal);
            const Document doc = make_queue_doc(
                    backend_token, terminal_token, route_token, routed, steps_of(kind),
                    route_token, routed, false, false);
            expect_combination("queue == token", doc, kind);
        }
    }

    /* ---- seams (design doc 5-Q3) ---- */
    {
        std::vector<Value> elements = {seam_element("pre_spawn")};
        for (Value &element : steps_of(CombinationKind::PselectRootchild)) {
            elements.push_back(std::move(element));
        }
        elements.push_back(seam_element("post_terminal"));
        const Document doc = make_queue_doc("cve_2026_43499", "root_child",
                                            "select_stack", true, std::move(elements),
                                            "select_stack", true, false, false);
        expect_combination("seams around the steps", doc, CombinationKind::PselectRootchild);
    }

    /* ---- declaration-time rules, each with its named reason ---- */
    expect_reject("unknown step token",
                  make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                 {step_element("w9")}, "select_stack", true, false, false),
                  "unknown-step-token");
    expect_reject("cross-backend step",
                  make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                 {step_element("pagecache_write")}, "select_stack", true,
                                 false, false),
                  "cross-backend-step");
    expect_reject("per-step route",
                  make_queue_doc(
                          "cve_2026_43499", "root_child", "select_stack", true,
                          {map_value({member("step", str_value("w1")),
                                      member("route", str_value("select_stack"))})},
                          "select_stack", true, false, false),
                  "step-route-not-allowed");
    expect_reject("reserved params",
                  make_queue_doc(
                          "cve_2026_43499", "root_child", "select_stack", true,
                          {map_value({member("step", str_value("w1")),
                                      member("params", str_value("x"))})},
                          "select_stack", true, false, false),
                  "params-reserved-for-future-step-parameters");
    expect_reject("unknown element key",
                  make_queue_doc(
                          "cve_2026_43499", "root_child", "select_stack", true,
                          {map_value({member("step", str_value("w1")),
                                      member("surprise", str_value("x"))})},
                          "select_stack", true, false, false),
                  "queue-element-unknown-key");
    expect_reject("bare string element",
                  make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                 {str_value("w1")}, "select_stack", true, false, false),
                  "queue-element-not-object");
    expect_reject("duplicate step",
                  make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                 {step_element("w1"), step_element("w1")}, "select_stack",
                                 true, false, false),
                  "duplicate-step");
    expect_reject("missing dependency",
                  make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                 {step_element("w1"), step_element("w3")}, "select_stack",
                                 true, false, false),
                  "missing-dependency");
    expect_reject("missing queue-level route",
                  make_queue_doc("cve_2026_43499", "root_child", "", false,
                                 steps_of(CombinationKind::PselectRootchild), "", false, false,
                                 false),
                  "route-required");
    expect_reject("route on a route-less backend",
                  make_queue_doc("cve_2026_43284", "umh_forward", "", false,
                                 {step_element("pagecache_write")}, "select_stack", true,
                                 false, false),
                  "route-not-applicable");
    expect_reject("empty queue",
                  make_queue_doc("cve_2026_43284", "umh_forward", "", false, {}, "", false,
                                 false, false),
                  "empty-queue");

    /* ---- M5: the removed token sugar is REFUSED, and named ----
     * `backend.<id>.steps = "<combination token>"` was the migration sugar;
     * `queue` (+ its queue-level `route`) is the only selection surface now.
     * The key stays recognised so the refusal names the path instead of
     * degrading into a generic unknown-key error. */
    {
        Document doc = make_queue_doc("cve_2026_43499", "root_child", "multicast_waiter",
                                      true, {}, "multicast_waiter", true, false, false);
        Section &section = doc.sections.back();
        section.entries.clear();
        section.entries.push_back(Entry{"steps", str_value("mcast_rootchild")});
        expect_reject("token form (43499)", doc, "token-form-removed");
    }
    {
        Document doc = make_queue_doc("cve_2026_43284", "umh_forward", "", false, {}, "",
                                      false, false, false);
        Section &section = doc.sections.back();
        section.entries.clear();
        section.entries.push_back(Entry{"steps", str_value("umh")});
        expect_reject("token form (43284)", doc, "token-form-removed");
    }

    /* ---- experimental: declared request, refused execution (design 4.3) ---- */
    expect_reject("experimental without declaration",
                  make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                 {step_element("w1")}, "select_stack", true, false, false),
                  "experimental-not-declared");
    expect_reject("experimental declared",
                  make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                 {step_element("w1")}, "select_stack", true, true, true),
                  "experimental-not-verified");

    /* ---- queue and token together, and the root-route agreement ---- */
    {
        Document doc = make_queue_doc("cve_2026_43499", "root_child", "select_stack", true,
                                      steps_of(CombinationKind::PselectRootchild),
                                      "select_stack", true, false, false);
        Section *section = doc.find_section("backend.cve_2026_43499");
        assert(section != nullptr);
        section->entries.push_back(Entry{"steps", str_value("pselect_rootchild")});
        expect_reject("queue and token together", doc, "queue-and-token-both-present");
    }
    expect_reject("root route disagrees with the queue route",
                  make_queue_doc("cve_2026_43499", "root_child", "multicast_waiter", true,
                                 steps_of(CombinationKind::PselectRootchild),
                                 "select_stack", true, false, false),
                  "route-disagrees-with-root");

    /* ---- M5: the two former TOKEN cases are refusals now, and the semantics
     * they carried live on the QUEUE path:
     *   - route agreement: `route-disagrees-with-root` above (root route vs the
     *     queue-derived preset route), plus `route-required` (43499 has a route
     *     axis) and `route-not-applicable` (43284 has none);
     *   - terminal agreement: the queue path cross-checks the declared terminal
     *     against the preset, so a mismatching terminal is still refused (that
     *     check reports no named reason - a pre-existing silent point). ---- */
    {
        Document doc;
        doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
        doc.has_release = true;
        doc.release = "6.6.77-queue-test";
        doc.has_terminal = true;
        doc.terminal = "root_child";
        doc.has_backend = true;
        doc.backend = "cve_2026_43499";
        doc.has_route = true;
        doc.route = "select_stack";
        Section &section = doc.append_section("backend.cve_2026_43499");
        section.entries.push_back(Entry{"steps", str_value("pselect_rootchild")});
        expect_reject("token path (was: token path unchanged)", doc,
                      "token-form-removed");
    }
    {
        Document doc;
        doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
        doc.has_release = true;
        doc.release = "6.6.77-queue-test";
        doc.has_terminal = true;
        doc.terminal = "root_child";
        doc.has_backend = true;
        doc.backend = "cve_2026_43499";
        doc.has_route = true;
        doc.route = "select_stack";
        Section &section = doc.append_section("backend.cve_2026_43499");
        section.entries.push_back(Entry{"route", str_value("multicast_waiter")});
        section.entries.push_back(Entry{"steps", str_value("pselect_rootchild")});
        expect_reject("section route beside a token (was: disagrees with the token)",
                      doc, "token-form-removed");
    }
    {
        /* The migrated terminal check: the queue is a supported preset, but the
         * declared terminal is not the preset's terminal. */
        const Document doc =
                make_queue_doc("cve_2026_43499", "umh_forward", "select_stack", true,
                               steps_of(CombinationKind::PselectRootchild), "select_stack",
                               true, false, false);
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) != 0);
    }

    /* ---- declaration-driven: an Array on an undeclared key stays rejected ---- */
    {
        Document doc;
        doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
        doc.has_release = true;
        doc.release = "6.6.77-queue-test";
        doc.has_terminal = true;
        doc.terminal = "root_child";
        doc.has_backend = true;
        doc.backend = "cve_2026_43499";
        doc.has_route = true;
        doc.route = "select_stack";
        Section &section = doc.append_section("backend.cve_2026_43499");
        /* A valid QUEUE keeps the rest of the document acceptable, so the ONLY
         * reason to reject is the array on an undeclared key. */
        section.entries.push_back(Entry{"route", str_value("select_stack")});
        section.entries.push_back(
                Entry{"queue", array_value({step_element("w1"), step_element("w2")})});
        section.entries.push_back(Entry{"surprise", array_value({step_element("w1")})});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) != 0);
    }

    if (failures != 0) {
        std::fprintf(stderr, "queue_wire_test: FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("queue_wire_test: ok (same-shape corpus + 14 named rules)");
    return 0;
}