/* S4 R6b combination-token framing test - M5 update.
 *
 * The user-visible token form is GONE (M5, design 4.4 / 11-U7): a wire
 * `backend.<id>.steps = "<token>"` is refused with the NAMED reason
 * `plan_error reason=token-form-removed`, and `queue` (plus its queue-level
 * `route`) is the only selection surface. This file therefore asserts:
 *   - every formerly ACCEPTED token spelling is refused, by name;
 *   - a terminal / root route can no longer "disagree with the token" (there is
 *     no token to disagree with) - the queue path owns those rules now;
 *   - the legacy uint step id keeps its decode path and lands on the
 *     catalogue's own combination;
 *   - the internal catalogue (token <-> route / steps / path / terminal) is
 *     unchanged and stays the single authority - only the wire syntax went.
 * The queue path itself (preset equivalence) lives in queue_wire_test.cpp. */

#include "contract/identity.hpp"
#include "profile/glkv3.hpp"
#include "profile/glkv3_parse.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

using ghostlock::contract::BackendKind;
using ghostlock::contract::CombinationKind;
using ghostlock::contract::CombinationSpec;
using ghostlock::contract::StepSetKind;
using ghostlock::contract::TerminalKind;
using ghostlock::profile::glkv3::Document;
using ghostlock::profile::glkv3::Entry;
using ghostlock::profile::glkv3::Section;
using ghostlock::profile::glkv3::Value;
using ghostlock::profile::glkv3::WireType;

namespace {
    Value uint_value(uint64_t raw) {
        Value value;
        value.type = WireType::UInt;
        value.uint_value = raw;
        return value;
    }

    Value str_value(std::string_view text) {
        Value value;
        value.type = WireType::Str;
        value.bytes = text;
        return value;
    }

    Document make_doc(std::string_view backend, std::string_view terminal,
                      std::string_view route, bool has_route,
                      std::string_view section, Entry steps) {
        Document doc;
        doc.schema = ghostlock::profile::glkv3::kSchemaVersion;
        doc.has_release = true;
        doc.release = "6.6.77-combination-test";
        doc.has_terminal = true;
        doc.terminal = terminal;
        doc.has_backend = true;
        doc.backend = backend;
        if (has_route) {
            doc.has_route = true;
            doc.route = route;
        }
        Section &target = doc.append_section(section);
        target.entries.push_back(std::move(steps));
        return doc;
    }

    int frame(const Document &doc, ghostlock::profile::Document &out) {
        const std::string encoded = ghostlock::profile::glkv3::encode(doc);
        if (encoded.empty()) return -1;
        return ghostlock::profile::frame_v3(encoded, &out);
    }

    const CombinationSpec &spec(CombinationKind kind) {
        const CombinationSpec *found = ghostlock::contract::combination_spec(kind);
        assert(found != nullptr);
        return *found;
    }

    /* Capture stderr so the named diagnostic is asserted, not assumed. */
    std::string capture_stderr(const Document &doc, int &status) {
        char path[] = "/tmp/glk_token_diag_XXXXXX";
        const int fd = mkstemp(path);
        assert(fd >= 0);
        const int saved = dup(STDERR_FILENO);
        assert(saved >= 0);
        assert(dup2(fd, STDERR_FILENO) >= 0);
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

    /* M5: the token form must be REFUSED BY NAME. */
    void expect_token_removed(const char *what, const Document &doc) {
        int status = 0;
        const std::string text = capture_stderr(doc, status);
        if (status == 0 ||
            text.find("plan_error reason=token-form-removed") == std::string::npos) {
            std::fprintf(
                    stderr,
                    "combination_token_test: %s: expected the removed token form to be "
                    "refused with plan_error reason=token-form-removed, status=%d\n",
                    what, status);
            std::abort();
        }
    }
} // namespace

int main() {
    using ghostlock::profile::kRouteAuto;
    using ghostlock::profile::kRouteMulticastWaiter;
    using ghostlock::profile::kRouteNone;
    using ghostlock::profile::kRouteSelectStack;

    /* ---- M5: every formerly ACCEPTED token spelling is refused, by name ----
     * These blocks used to assert that a wired token resolved and derived its
     * route / step set / terminal; the wire token is gone, so each now asserts
     * the REFUSAL (captured from stderr: status != 0 AND
     * `plan_error reason=token-form-removed`), while the contract-level
     * expectations that do not depend on the wire syntax stay asserted. */
    {
        expect_token_removed("wired token",
                             make_doc("cve_2026_43499", "root_child", "multicast_waiter",
                                      true, "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")}));
        assert(spec(CombinationKind::McastRootchild).steps == StepSetKind::W1W3);
        assert(spec(CombinationKind::McastRootchild).terminal == TerminalKind::RootChild);
    }
    {
        expect_token_removed("planned token",
                             make_doc("cve_2026_43499", "umh_forward", "multicast_waiter",
                                      true, "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_umh")}));
        assert(!ghostlock::contract::combination_available(CombinationKind::McastUmh));
    }
    {
        expect_token_removed("43284 bare-path token",
                             make_doc("cve_2026_43284", "root_child", "", false,
                                      "backend.cve_2026_43284",
                                      Entry{"steps", str_value("rootchild")}));
        assert(!ghostlock::contract::combination_available(CombinationKind::Rootchild));
        /* F3: a backend without a route axis carries None, not the legacy Auto. */
        assert(spec(CombinationKind::Rootchild).route ==
               ghostlock::profile::RouteKind::None);
    }
    {
        /* The unknown-token distinction is gone with the syntax: every string
         * here is refused by the same named rule. */
        expect_token_removed("unknown token",
                             make_doc("cve_2026_43499", "root_child", "multicast_waiter",
                                      true, "backend.cve_2026_43499",
                                      Entry{"steps", str_value("bogus_token")}));
    }
    /* A terminal or a root route can no longer disagree with the token - there
     * is no token to disagree with. Those rules live on the QUEUE path now:
     * terminal agreement inside resolve_queue, root-route agreement as
     * `route-disagrees-with-root`, and the route-axis rules as `route-required` /
     * `route-not-applicable` (all asserted in queue_wire_test.cpp). */
    {
        expect_token_removed("terminal disagreement (was: rejected)",
                             make_doc("cve_2026_43499", "umh_forward", "multicast_waiter",
                                      true, "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")}));
    }
    {
        expect_token_removed("root route disagreement (was: rejected)",
                             make_doc("cve_2026_43499", "root_child", "select_stack", true,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")}));
    }
    {
        expect_token_removed("route-less document (was: rejected)",
                             make_doc("cve_2026_43499", "root_child", "", false,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")}));
    }
    {
        expect_token_removed("route on a route-less backend (was: rejected)",
                             make_doc("cve_2026_43284", "umh_forward", "multicast_waiter",
                                      true, "backend.cve_2026_43284",
                                      Entry{"steps", str_value("umh")}));
    }
    {
        expect_token_removed("wired 43284 token",
                             make_doc("cve_2026_43284", "umh_forward", "", false,
                                      "backend.cve_2026_43284",
                                      Entry{"steps", str_value("umh")}));
        assert(spec(CombinationKind::Umh).route == ghostlock::profile::RouteKind::None);
    }

    /* ---- Legacy uint 1 (W1W2) maps onto the route's shizuku token. ---- */
    {
        const Document doc = make_doc("cve_2026_43499", "root_child",
                                      "select_stack", true,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", uint_value(1)});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) == 0);
        assert(framed.combination ==
               static_cast<uint8_t>(CombinationKind::PselectShizuku));
        const ghostlock::profile::Value *steps =
                framed.find_value("backend.cve_2026_43499", "steps");
        assert(steps != nullptr && steps->is_text && steps->text == "pselect_shizuku");
        assert(spec(CombinationKind::PselectShizuku).steps == StepSetKind::W1W2);
    }

    /* ---- Legacy uint 3 (PageCacheWrite) on 43284 maps to "umh". ---- */
    {
        const Document doc = make_doc("cve_2026_43284", "umh_forward", "", false,
                                      "backend.cve_2026_43284",
                                      Entry{"steps", uint_value(3)});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) == 0);
        assert(framed.combination == static_cast<uint8_t>(CombinationKind::Umh));
        assert(ghostlock::contract::combination_available(CombinationKind::Umh));
        const ghostlock::profile::Value *steps =
                framed.find_value("backend.cve_2026_43284", "steps");
        assert(steps != nullptr && steps->is_text && steps->text == "umh");
    }

    /* ---- M5: the LEGACY uint wire still decodes and lands on the catalogue
     * entry itself. The modern counterpart is the QUEUE path (queue_wire_test
     * .cpp owns the same-shape corpus and the route/terminal agreement). ---- */
    struct Equivalence {
        const char *token;
        const char *route;
        uint64_t legacy_id;
    };
    const Equivalence equivalences[] = {
        {"mcast_rootchild", "multicast_waiter", 2},
        {"pselect_rootchild", "select_stack", 2},
        {"tcp_rootchild", "tcp_zerocopy", 2},
        {"mcast_shizuku", "multicast_waiter", 1},
        {"pselect_shizuku", "select_stack", 1},
        {"tcp_shizuku", "tcp_zerocopy", 1},
    };
    for (const Equivalence &entry : equivalences) {
        CombinationKind expected = CombinationKind::Unknown;
        assert(ghostlock::contract::combination_resolve(
                BackendKind::Cve2026_43499, entry.token, expected));
        const Document legacy =
                make_doc("cve_2026_43499", "root_child", entry.route, true,
                         "backend.cve_2026_43499",
                         Entry{"steps", uint_value(entry.legacy_id)});
        ghostlock::profile::Document framed;
        assert(frame(legacy, framed) == 0);
        assert(framed.combination == static_cast<uint8_t>(expected));
        /* The legacy path keeps the key the CALLER sent and rewrites it to the
         * catalogue token for the String bind; M5 removed only the QUEUE path
         * write-back, where no such key existed. */
        const ghostlock::profile::Value *steps =
                framed.find_value("backend.cve_2026_43499", "steps");
        assert(steps != nullptr && steps->is_text && steps->text == entry.token);
    }
    {
        const Document legacy = make_doc("cve_2026_43284", "umh_forward", "", false,
                                         "backend.cve_2026_43284",
                                         Entry{"steps", uint_value(3)});
        ghostlock::profile::Document framed;
        assert(frame(legacy, framed) == 0);
        assert(framed.combination == static_cast<uint8_t>(CombinationKind::Umh));
        const ghostlock::profile::Value *steps =
                framed.find_value("backend.cve_2026_43284", "steps");
        assert(steps != nullptr && steps->is_text && steps->text == "umh");
    }

    /* ---- Token <-> (route, steps, path) derivation is one authority. ---- */
    for (const CombinationSpec &entry : ghostlock::contract::kCombinationCatalog) {
        assert(ghostlock::contract::combination_name(entry.kind) == entry.token);
        assert(spec(entry.kind).backend == entry.backend);
    }

    /* ---- F1 decomposition: (backend, route, path) is a lossless view. ---- */
    for (const CombinationSpec &entry : ghostlock::contract::kCombinationCatalog) {
        ghostlock::contract::CombinationId id{};
        assert(ghostlock::contract::combination_id(entry.kind, id));
        assert(id.backend == entry.backend);
        assert(id.route == entry.route);
        assert(id.path == entry.path);
        assert(ghostlock::contract::combination_from_id(id) == entry.kind);
        assert(!entry.doc.empty());
        assert(!ghostlock::contract::path_name(entry.path).empty());
        assert(!ghostlock::contract::route_name(entry.route).empty());
        /* The path axis is NOT the terminal axis: shizuku still enters the
         * root child, so a terminal-only model would alias it with rootchild. */
        if (entry.path == ghostlock::contract::PathKind::Shizuku) {
            assert(entry.terminal == ghostlock::contract::TerminalKind::RootChild);
        }
    }
    {
        ghostlock::contract::CombinationId id{};
        assert(ghostlock::contract::combination_id(
                ghostlock::contract::CombinationKind::McastRootchild, id));
        assert(id.backend == ghostlock::contract::BackendKind::Cve2026_43499);
        assert(id.route == ghostlock::profile::RouteKind::MulticastWaiter);
        assert(id.path == ghostlock::contract::PathKind::Rootchild);
    }
    {
        ghostlock::contract::CombinationId id{};
        assert(ghostlock::contract::combination_id(
                ghostlock::contract::CombinationKind::Umh, id));
        assert(id.route == ghostlock::profile::RouteKind::None);
        assert(id.path == ghostlock::contract::PathKind::Umh);
        assert(ghostlock::contract::path_name(id.path) == "umh");
        assert(ghostlock::contract::route_name(id.route) == "none");
    }
    {
        /* The one route spelling table: catalogue, manifests and
         * pipeline::middleware_name all read these strings. */
        using ghostlock::profile::RouteKind;
        assert(ghostlock::contract::route_name(RouteKind::TcpZerocopy) ==
               "tcp_zerocopy");
        assert(ghostlock::contract::route_name(RouteKind::SelectStack) == "select_stack");
        assert(ghostlock::contract::route_name(RouteKind::MulticastWaiter) ==
               "multicast_waiter");
        assert(ghostlock::contract::route_name(RouteKind::None) == "none");
        /* The deprecated legacy value 0 has no enumerator and reports as none. */
        assert(ghostlock::contract::route_name(static_cast<RouteKind>(0)) == "none");
    }
    {
        /* Unknown is not a row: no id, and the reverse lookup stays Unknown. */
        ghostlock::contract::CombinationId id{};
        assert(!ghostlock::contract::combination_id(
                ghostlock::contract::CombinationKind::Unknown, id));
        assert(ghostlock::contract::combination_from_id(
                   ghostlock::contract::CombinationId{
                       ghostlock::contract::BackendKind::Cve2026_64560,
                       ghostlock::profile::RouteKind::None,
                       ghostlock::contract::PathKind::Umh}) ==
               ghostlock::contract::CombinationKind::Unknown);
    }
    puts("combination_token_test: ok");
    return 0;
}
