/* S4 R6b combination-token framing test.
 *
 * Exercises profile/glkv3_parse.cpp end to end over real GLKv3 bytes:
 *   - a wired token resolves and derives route/steps/terminal;
 *   - a planned token parses but stays unavailable;
 *   - an unknown token is rejected (fail closed);
 *   - a terminal or root route that disagrees with the token is rejected;
 *   - the legacy uint step id maps onto the equivalent token.
 * The token <-> (route, steps, path) derivation is asserted here too. */

#include "contract/identity.hpp"
#include "profile/glkv3.hpp"
#include "profile/glkv3_parse.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

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
} // namespace

int main() {
    using ghostlock::profile::kRouteAuto;
    using ghostlock::profile::kRouteMulticastWaiter;
    using ghostlock::profile::kRouteNone;
    using ghostlock::profile::kRouteSelectStack;

    /* ---- Wired token resolves and derives the triple. ---- */
    {
        const Document doc = make_doc("cve_2026_43499", "root_child",
                                      "multicast_waiter", true,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) == 0);
        assert(framed.combination ==
               static_cast<uint8_t>(CombinationKind::McastRootchild));
        assert(framed.middleware == kRouteMulticastWaiter);
        assert(framed.terminal_token == "root_child");
        const ghostlock::profile::Value *steps =
                framed.find_value("backend.cve_2026_43499", "steps");
        assert(steps != nullptr && steps->is_text && steps->text == "mcast_rootchild");
        assert(spec(CombinationKind::McastRootchild).steps == StepSetKind::W1W3);
        assert(spec(CombinationKind::McastRootchild).terminal == TerminalKind::RootChild);
    }

    /* ---- A planned token parses, but the selection gate rejects it. ---- */
    {
        const Document doc = make_doc("cve_2026_43499", "umh_forward",
                                      "multicast_waiter", true,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_umh")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) == 0);
        assert(framed.combination == static_cast<uint8_t>(CombinationKind::McastUmh));
        assert(!ghostlock::contract::combination_available(CombinationKind::McastUmh));
        assert(!ghostlock::contract::combination_available(
                static_cast<CombinationKind>(framed.combination)));
    }

    /* ---- 43284 planned bare-path token. ---- */
    {
        const Document doc = make_doc("cve_2026_43284", "root_child", "", false,
                                      "backend.cve_2026_43284",
                                      Entry{"steps", str_value("rootchild")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) == 0);
        assert(framed.combination == static_cast<uint8_t>(CombinationKind::Rootchild));
        assert(!ghostlock::contract::combination_available(CombinationKind::Rootchild));
        /* F3: a backend without a route axis carries None, not the legacy Auto. */
        assert(framed.middleware == kRouteNone);
    }

    /* ---- Unknown token: rejected. ---- */
    {
        const Document doc = make_doc("cve_2026_43499", "root_child",
                                      "multicast_waiter", true,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("bogus_token")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) != 0);
    }

    /* ---- Terminal that disagrees with the token: rejected. ---- */
    {
        const Document doc = make_doc("cve_2026_43499", "umh_forward",
                                      "multicast_waiter", true,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) != 0);
    }

    /* ---- Root route that disagrees with the token: rejected. ---- */
    {
        const Document doc = make_doc("cve_2026_43499", "root_child",
                                      "select_stack", true,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) != 0);
    }

    /* ---- F3: a token that requires a route rejects a route-less document. ---- */
    {
        const Document doc = make_doc("cve_2026_43499", "root_child", "", false,
                                      "backend.cve_2026_43499",
                                      Entry{"steps", str_value("mcast_rootchild")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) != 0);
    }

    /* ---- F3: a backend without a route axis rejects a named route. ---- */
    {
        const Document doc = make_doc("cve_2026_43284", "umh_forward",
                                      "multicast_waiter", true,
                                      "backend.cve_2026_43284",
                                      Entry{"steps", str_value("umh")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) != 0);
    }

    /* ---- F3: the wired 43284 token derives RouteKind::None. ---- */
    {
        const Document doc = make_doc("cve_2026_43284", "umh_forward", "", false,
                                      "backend.cve_2026_43284",
                                      Entry{"steps", str_value("umh")});
        ghostlock::profile::Document framed;
        assert(frame(doc, framed) == 0);
        assert(framed.middleware == kRouteNone);
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

    /* ---- Wired behaviour equivalence: the old uint wire and the new token
     * wire resolve to the exact same route / step set / terminal. ---- */
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
        const Document modern =
                make_doc("cve_2026_43499", "root_child", entry.route, true,
                         "backend.cve_2026_43499",
                         Entry{"steps", str_value(entry.token)});
        const Document legacy =
                make_doc("cve_2026_43499", "root_child", entry.route, true,
                         "backend.cve_2026_43499",
                         Entry{"steps", uint_value(entry.legacy_id)});
        ghostlock::profile::Document old_style;
        ghostlock::profile::Document new_style;
        assert(frame(modern, new_style) == 0);
        assert(frame(legacy, old_style) == 0);
        assert(new_style.combination == old_style.combination);
        assert(new_style.middleware == old_style.middleware);
        assert(new_style.terminal_token == old_style.terminal_token);
        const ghostlock::profile::Value *new_steps =
                new_style.find_value("backend.cve_2026_43499", "steps");
        const ghostlock::profile::Value *old_steps =
                old_style.find_value("backend.cve_2026_43499", "steps");
        assert(new_steps != nullptr && old_steps != nullptr);
        assert(new_steps->is_text && old_steps->is_text);
        assert(new_steps->text == old_steps->text);
    }
    {
        const Document modern = make_doc("cve_2026_43284", "umh_forward", "", false,
                                         "backend.cve_2026_43284",
                                         Entry{"steps", str_value("umh")});
        const Document legacy = make_doc("cve_2026_43284", "umh_forward", "", false,
                                         "backend.cve_2026_43284",
                                         Entry{"steps", uint_value(3)});
        ghostlock::profile::Document new_style;
        ghostlock::profile::Document old_style;
        assert(frame(modern, new_style) == 0);
        assert(frame(legacy, old_style) == 0);
        assert(new_style.combination == old_style.combination);
        assert(new_style.middleware == old_style.middleware);
        assert(new_style.terminal_token == old_style.terminal_token);
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
