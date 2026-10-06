#include "profile/glkv3_parse.hpp"

#include "contract/identity.hpp"
#include "contract/model.hpp"
#include "contract/step_plan.hpp"
#include "profile/document.hpp"
#include "profile/glkv3.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace ghostlock::profile {
    namespace {
//         /* S4 payload owner (contract-design 3.15; design r2). Shape is ONE flat
//          * section "payload" with dotted keys; every rule is fail-closed and the
//          * section is OPTIONAL: without it the parse result is unchanged. */
//         constexpr std::size_t kPayloadMaxString = 256u;
//         constexpr std::uint64_t kPayloadMaxKo = 8u;
// 
//         bool payload_path_ok(std::string_view path) noexcept {
//             if (path.empty() || path.size() > kPayloadMaxString) return false;
//             if (path.front() == '/') return false;
//             if (path.find("..") != std::string_view::npos) return false;
//             if (path.find('\\') != std::string_view::npos) return false;
//             for (const char ch : path) {
//                 const unsigned char raw = static_cast<unsigned char>(ch);
//                 if (raw < 0x20u || raw == 0x7fu) return false;
//             }
//             return true;
//         }
// 
//         bool payload_hash_ok(std::string_view hash) noexcept {
//             if (hash.size() != 64u) return false;
//             for (const char ch : hash) {
//                 const bool digit = ch >= '0' && ch <= '9';
//                 const bool lower = ch >= 'a' && ch <= 'f';
//                 if (!digit && !lower) return false;
//             }
//             return true;
//         }
// 
//         bool payload_sha_ok(const profile::Section &section, std::string_view key) noexcept {
//             const profile::Value *value = section.find(key);
//             if (value == nullptr) return true; /* optional */
//             return value->is_text && payload_hash_ok(value->text);
//         }
// 
//         bool payload_text_ok(const profile::Value &value) noexcept {
//             if (!value.is_text || value.text.empty() || value.text.size() > kPayloadMaxString) {
//                 return false;
//             }
//             for (const char ch : value.text) {
//                 const unsigned char raw = static_cast<unsigned char>(ch);
//                 if (raw < 0x20u || raw == 0x7fu) return false;
//             }
//             return true;
//         }
// 
//         /* Splits "ko.<i>.path" / "ko.<i>.sha256": the ko index is 1..2 decimal
//          * digits. The "ko." node is REQUIRED (design r3, ruling 2026-10-05): the
//          * index lives under the same node as ko.count, so a bare "<i>.path" --
//          * the earlier spelling -- or any other prefix is an unknown key and is
//          * rejected fail-closed below. */
//         bool payload_split_index(std::string_view key, std::string_view suffix,
//                                  std::uint64_t &index) noexcept {
//             constexpr std::string_view kKoPrefix = "ko.";
//             if (!key.starts_with(kKoPrefix)) return false;
//             key.remove_prefix(kKoPrefix.size());
//             if (key.size() <= suffix.size()) return false;
//             if (key.substr(key.size() - suffix.size()) != suffix) return false;
//             const std::string_view head = key.substr(0u, key.size() - suffix.size());
//             if (head.empty() || head.size() > 2u) return false;
//             std::uint64_t value = 0u;
//             for (const char ch : head) {
//                 if (ch < '0' || ch > '9') return false;
//                 value = value * 10u + static_cast<std::uint64_t>(ch - '0');
//             }
//             index = value;
//             return true;
//         }
// 
//         bool validate_payload_section(const profile::Section &section) noexcept {
//             const profile::Value *tier = section.find("tier");
//             if (tier == nullptr || !tier->is_text) return false;
//             const std::string_view tier_token = tier->text;
//             const bool is_exec = tier_token == "exec";
//             const bool is_script = tier_token == "script";
//             const bool is_ko = tier_token == "ko";
//             if (!is_exec && !is_script && !is_ko) return false;
// 
//             std::uint64_t declared_count = 0u;
//             bool have_count = false;
//             std::uint64_t seen_paths = 0u;
//             std::uint64_t path_bits = 0u;
//             for (const profile::Entry &entry : section.entries) {
//                 const std::string_view key = entry.key;
//                 if (key == "tier") continue;
//                 if (key == "exec.command") {
//                     if (!is_exec || !payload_text_ok(entry.value)) return false;
//                     continue;
//                 }
//                 if (key == "exec.sha256" || key == "script.sha256") {
//                     const bool owner_ok = key == "exec.sha256" ? is_exec : is_script;
//                     if (!owner_ok || !entry.value.is_text ||
//                         !payload_hash_ok(entry.value.text)) return false;
//                     continue;
//                 }
//                 if (key == "script.path") {
//                     if (!is_script || !entry.value.is_text ||
//                         !payload_path_ok(entry.value.text)) return false;
//                     continue;
//                 }
//                 if (key == "ko.count") {
//                     if (!is_ko || entry.value.is_text || !entry.value.present) return false;
//                     declared_count = entry.value.raw;
//                     have_count = true;
//                     continue;
//                 }
//                 std::uint64_t index = 0u;
//                 if (payload_split_index(key, ".path", index)) {
//                     if (!is_ko || index >= kPayloadMaxKo || !entry.value.is_text ||
//                         !payload_path_ok(entry.value.text)) return false;
//                     if ((path_bits & (std::uint64_t{1} << index)) != 0u) return false;
//                     path_bits |= std::uint64_t{1} << index;
//                     ++seen_paths;
//                     continue;
//                 }
//                 if (payload_split_index(key, ".sha256", index)) {
//                     if (!is_ko || index >= kPayloadMaxKo || !entry.value.is_text ||
//                         !payload_hash_ok(entry.value.text)) return false;
//                     continue;
//                 }
//                 return false;
//             }
// 
//             if (is_exec) {
//                 return section.find("exec.command") != nullptr &&
//                        payload_sha_ok(section, "exec.sha256");
//             }
//             if (is_script) {
//                 return section.find("script.path") != nullptr &&
//                        payload_sha_ok(section, "script.sha256");
//             }
//             if (!have_count || declared_count == 0u || declared_count > kPayloadMaxKo) {
//                 return false;
//             }
//             return seen_paths == declared_count;
//         }
        /* M2: materialise a declared array field into the neutral Document
         * composite. Bounds are structural; element maps carry scalar members
         * only, and a non-scalar member is recorded as an unsupported member so
         * the queue normalizer can name the precise reason. */
        bool materialise_array(const profile::glkv3::Value &array,
                               std::vector<profile::CompositeItem> &out) {
            if (array.elements.size() > profile::kMaxCompositeItems) return false;
            out.reserve(array.elements.size());
            for (const profile::glkv3::Value &element : array.elements) {
                profile::CompositeItem item;
                if (element.type != profile::glkv3::WireType::Map) {
                    /* Bare-string element (design doc U9) or any other non-map
                     * element: recorded, not silently dropped. */
                    item.is_map = false;
                    out.push_back(std::move(item));
                    continue;
                }
                if (element.members.size() > profile::kMaxCompositeKeys) return false;
                item.entries.reserve(element.members.size());
                for (const profile::glkv3::MapMember &member : element.members) {
                    profile::CompositeEntry entry;
                    entry.key.assign(member.key.data(), member.key.size());
                    if (entry.key.size() > profile::kMaxCompositeTextBytes) return false;
                    switch (member.type) {
                        case profile::glkv3::WireType::Str:
                            entry.is_text = true;
                            if (member.bytes.size() > profile::kMaxCompositeTextBytes) {
                                return false;
                            }
                            entry.text.assign(member.bytes.data(), member.bytes.size());
                            break;
                        case profile::glkv3::WireType::UInt:
                            entry.raw = member.uint_value;
                            break;
                        case profile::glkv3::WireType::Int:
                            entry.raw = static_cast<uint64_t>(member.int_value);
                            break;
                        case profile::glkv3::WireType::Bool:
                            entry.raw = member.bool_value ? uint64_t{1} : uint64_t{0};
                            break;
                        default:
                            /* Nested map/array/bin/union: outside the element
                             * vocabulary; kept as a named unsupported member. */
                            entry.unsupported = true;
                            break;
                    }
                    item.entries.push_back(std::move(entry));
                }
                out.push_back(std::move(item));
            }
            return true;
        }

        /* HOCON refactor owner whitelist (contract-design 3.16): the only
         * owner-qualified section family is "backend.<id>.*" -- the ABI keys are
         * backend.cve_2026_43499.abi.* now. A section with any other prefix is
         * rejected here, fail-closed, before any owner bind. */
        bool known_owner_section(std::string_view name) {
            /* Removed owners, all rejected on sight:
             *   - "common": its keys became root scalars (kernel_major /
             *     safe_mode) or disappeared (vr_guard);
             *   - "platform.*": platform.abi.* moved to
             *     backend.cve_2026_43499.abi.*;
             *   - "countermeasure.*": the vr_guard fields were deleted, so the
             *     owner became empty;
             *   - "plugin" / "payload": paused by USER DIRECTIVE 2026-10-05;
             *   - "selection.*" / the root scalars themselves are not sections:
             *     a root key placed inside one is an unknown key and the owner
             *     bind rejects it. */
            return name.starts_with("backend.");
        }

        /* S4 R6b legacy compatibility: the old uint step id (1 = W1W2,
         * 2 = W1W3, 3 = PageCacheWrite) maps onto the equivalent token using the
         * root route, so a pre-R6b document keeps its exact behaviour. */
        bool legacy_43499_kind(RouteKind route, uint64_t id,
                               contract::CombinationKind &out) noexcept {
            const bool shizuku = id == 1;  /* W1W2 */
            const bool rootchild = id == 2; /* W1W3 */
            if (!shizuku && !rootchild) return false;
            switch (route) {
                case RouteKind::MulticastWaiter:
                    out = shizuku ? contract::CombinationKind::McastShizuku
                                  : contract::CombinationKind::McastRootchild;
                    return true;
                case RouteKind::SelectStack:
                    out = shizuku ? contract::CombinationKind::PselectShizuku
                                  : contract::CombinationKind::PselectRootchild;
                    return true;
                case RouteKind::TcpZerocopy:
                    out = shizuku ? contract::CombinationKind::TcpShizuku
                                  : contract::CombinationKind::TcpRootchild;
                    return true;
                default:
                    return false;
            }
        }

        bool legacy_43284_kind(uint64_t id, contract::CombinationKind &out) noexcept {
            if (id != 3) return false; /* PageCacheWrite */
            out = contract::CombinationKind::Umh;
            return true;
        }

        /* M2 canonical selection: backend.<id>.queue is an array of map and
         * route is its queue-level sibling. The framed composite feeds the M1.1
         * normalizer; every declaration-time rule fails closed with its named
         * `plan_error reason=` token. A supported plan installs the preset on the
         * NEUTRAL carrier (`Document::combination` / `middleware` /
         * `terminal_token`), which is what the orchestrator dispatches on, and
         * writes NO token back into the document (M5): the wire shape stays
         * `route` + `queue`, so a normalised document still re-parses cleanly and
         * never grows a `steps` field of its own. An experimental plan is reported and
         * refused: it has no compile-time pipeline instance yet (design doc
         * section 4.3 -- promotion needs its own device gate). */
        int32_t resolve_queue(Document *out, const glkv3::Document &decoded,
                              contract::BackendKind backend,
                              const Value &queue_value, const Value *route_value,
                              const Value *experimental_value) {
            const std::string_view backend_token = contract::backend_token_name(backend);
            std::vector<contract::QueueElement> elements;
            elements.reserve(queue_value.items.size());
            for (const CompositeItem &item : queue_value.items) {
                contract::QueueElement element;
                element.is_object = item.is_map;
                for (const CompositeEntry &member : item.entries) {
                    /* params / route are presence-only (reserved / forbidden); a
                     * non-textual step/seam/stage cannot name a token, so it is
                     * reported as an unknown member instead of being dropped. */
                    if (member.key == "params") {
                        element.has_params = true;
                        continue;
                    }
                    if (member.key == "route") {
                        element.has_step_route = true;
                        continue;
                    }
                    if (!member.is_text) {
                        if (element.unknown_key.empty()) element.unknown_key = member.key;
                        continue;
                    }
                    if (member.key == "step") {
                        element.step = member.text;
                    } else if (member.key == "seam") {
                        element.seam = member.text;
                    } else if (member.key == "stage") {
                        element.stage = member.text;
                    } else if (element.unknown_key.empty()) {
                        element.unknown_key = member.key;
                    }
                }
                elements.push_back(element);
            }

            contract::QueueInput input;
            input.backend = backend;
            input.elements = elements;
            if (route_value != nullptr && route_value->is_text) {
                input.route = route_value->text;
            }
            input.experimental_declared =
                    experimental_value != nullptr && experimental_value->present &&
                    experimental_value->raw != 0U;

            const contract::PlanResult result = contract::normalize_step_queue(input);
            if (!result.ok()) {
                (void)std::fprintf(stderr,
                                   "plan_error reason=%.*s at=%zu backend=%.*s\n",
                                   static_cast<int>(
                                           contract::plan_error_reason(result.error).size()),
                                   contract::plan_error_reason(result.error).data(),
                                   result.error_index,
                                   static_cast<int>(backend_token.size()),
                                   backend_token.data());
                return -1;
            }
            if (result.plan.verdict != contract::PlanVerdict::Supported) {
                (void)std::fprintf(stderr,
                                   "plan verdict=experimental declared=%d backend=%.*s\n",
                                   input.experimental_declared ? 1 : 0,
                                   static_cast<int>(backend_token.size()),
                                   backend_token.data());
                (void)std::fprintf(stderr,
                                   "plan_error reason=experimental-not-verified backend=%.*s\n",
                                   static_cast<int>(backend_token.size()),
                                   backend_token.data());
                return -1;
            }

            const contract::CombinationSpec *spec =
                    contract::combination_spec(result.plan.preset);
            if (spec == nullptr) return -1;
            /* The root route token, when the document carries one, must agree with
             * the queue-level route: two selection surfaces never disagree
             * silently (the route-less backend has RouteKind::None on both sides). */
            if (decoded.has_route) {
                const RouteKind root_route = static_cast<RouteKind>(
                        route_kind_from_string(decoded.route));
                if (root_route != spec->route) {
                    (void)std::fprintf(
                            stderr,
                            "plan_error reason=route-disagrees-with-root backend=%.*s\n",
                            static_cast<int>(backend_token.size()),
                            backend_token.data());
                    return -1;
                }
            }
            contract::TerminalKind terminal{};
            if (!contract::terminal_kind_from_token(decoded.terminal, terminal) ||
                terminal != spec->terminal) {
                return -1;
            }
            out->combination = static_cast<uint8_t>(spec->kind);
            out->middleware = static_cast<uint16_t>(spec->route);
            const std::string_view terminal_token =
                    contract::terminal_token_name(spec->terminal);
            out->terminal_token.assign(terminal_token.data(), terminal_token.size());
            /* M5: deliberately NO `steps` entry is materialised here. The plan
             * lives in memory on the fields above - that is what the orchestrator
             * dispatches on (`pipeline/orchestrator.hpp` reads
             * `document.combination`) - while writing the token back into the
             * section would put a REMOVED syntax on the wire and make the
             * normalised document un-reparsable (a re-parse refuses it with
             * `reason=token-form-removed`). The section is read-only here. */
            return 0;
        }

        /* M2/M5: select the plan from `backend.<id>`.
         *
         * `queue` (with its queue-level `route` sibling) is the ONLY selection
         * surface. `backend.<id>.steps` is no longer accepted as a combination
         * token (M5 removed the migration sugar): a string value there is
         * refused with a NAMED reason that points at the path, never silently
         * ignored. Declaring a queue AND a token is still reported as the
         * conflict it is. The key stays recognised on purpose: an unknown-key
         * rejection would name nothing and hide the migration from the log.
         * The token itself lives on internally as the normalised plan key
         * (resolve_queue writes it into the same slot) - only the user-visible
         * wire syntax is gone. The legacy uint id keeps its decode path. */
        int32_t resolve_combination(Document *out,
                                    const glkv3::Document &decoded) noexcept {
            contract::BackendKind backend{};
            if (!contract::backend_kind_from_token(decoded.backend, backend)) return -1;

            std::string section_name = "backend.";
            section_name.append(contract::backend_token_name(backend));
            Section *section = out->find_section(section_name);
            if (section == nullptr) return -1;
            Entry *steps_entry = nullptr;
            Entry *queue_entry = nullptr;
            Entry *route_entry = nullptr;
            Entry *experimental_entry = nullptr;
            for (Entry &entry : section->entries) {
                if (entry.key == "steps") {
                    steps_entry = &entry;
                } else if (entry.key == "queue") {
                    queue_entry = &entry;
                } else if (entry.key == "route") {
                    route_entry = &entry;
                } else if (entry.key == "experimental") {
                    experimental_entry = &entry;
                }
            }

            /* M2: the queue is the canonical selection; the token stays as sugar
             * for the in-repo assets. Declaring both at once is a conflict and
             * fails closed with a named reason. */
            if (queue_entry != nullptr) {
                if (steps_entry != nullptr) {
                    (void)std::fprintf(
                            stderr,
                            "plan_error reason=queue-and-token-both-present backend=%.*s\n",
                            static_cast<int>(
                                    contract::backend_token_name(backend).size()),
                            contract::backend_token_name(backend).data());
                    return -1;
                }
                if (!queue_entry->value.is_array) return -1;
                return resolve_queue(
                        out, decoded, backend, queue_entry->value,
                        route_entry != nullptr ? &route_entry->value : nullptr,
                        experimental_entry != nullptr ? &experimental_entry->value : nullptr);
            }
            if (steps_entry == nullptr) return -1;

            contract::CombinationKind combination = contract::CombinationKind::Unknown;
            if (steps_entry->value.is_text) {
                /* M5: the token form is removed. `reason` is the token the
                 * tests and the docs grep for; `path` names the exact key. */
                (void)std::fprintf(
                        stderr,
                        "plan_error reason=token-form-removed path=backend.%.*s.steps "
                        "hint=declare-route-and-queue\n",
                        static_cast<int>(contract::backend_token_name(backend).size()),
                        contract::backend_token_name(backend).data());
                return -1;
            } else {
                const uint64_t legacy_id = steps_entry->value.raw;
                (void)std::fprintf(stderr, "legacy_steps_id=%llu\n",
                                   static_cast<unsigned long long>(legacy_id));
                /* Legacy v1/v2 decode: an absent root route means "no route
                 * declared" (None). The legacy id resolver only accepts a
                 * catalogue route, so a 43499 legacy id without geometry fails
                 * closed and a 43284 legacy id is route-less by construction. */
                const RouteKind route =
                        decoded.has_route
                                ? static_cast<RouteKind>(
                                          route_kind_from_string(decoded.route))
                                : RouteKind::None;
                const bool legacy_ok =
                        backend == contract::BackendKind::Cve2026_43499
                                ? legacy_43499_kind(route, legacy_id, combination)
                                : (backend == contract::BackendKind::Cve2026_43284 &&
                                   legacy_43284_kind(legacy_id, combination));
                if (!legacy_ok) return -1;
            }

            const contract::CombinationSpec *spec = contract::combination_spec(combination);
            if (spec == nullptr) return -1;

            /* M2: a queue-level route written beside a token must name the route
             * the token declares (and a route-less backend must not carry one), so
             * the two selection surfaces can never disagree silently. */
            if (route_entry != nullptr) {
                const std::string_view declared_route_token =
                        route_entry->value.is_text ? route_entry->value.text
                                                   : std::string_view{};
                const bool applies = spec->route != RouteKind::None;
                const bool agrees =
                        applies && !declared_route_token.empty() &&
                        static_cast<RouteKind>(
                                route_kind_from_string(declared_route_token)) == spec->route;
                if (!agrees) {
                    (void)std::fprintf(
                            stderr,
                            "plan_error reason=%s backend=%.*s\n",
                            applies ? "route-disagrees-with-token" : "route-not-applicable",
                            static_cast<int>(
                                    contract::backend_token_name(backend).size()),
                            contract::backend_token_name(backend).data());
                    return -1;
                }
            }

            /* F3 fail-closed route agreement: the root route, when present,
             * must name the token's declared route; an absent route is None,
             * which a token that requires a route never matches. A backend
             * without a route axis (None) must not name one, and an unknown
             * route token resolves to None instead of the legacy Auto. */
            const RouteKind declared_route =
                    decoded.has_route
                            ? static_cast<RouteKind>(
                                      route_kind_from_string(decoded.route))
                            : RouteKind::None;
            if (declared_route != spec->route) return -1;

            contract::TerminalKind terminal{};
            if (!contract::terminal_kind_from_token(decoded.terminal, terminal) ||
                terminal != spec->terminal) {
                return -1;
            }

            out->combination = static_cast<uint8_t>(combination);
            out->middleware = static_cast<uint16_t>(spec->route);
            const std::string_view terminal_token =
                    contract::terminal_token_name(spec->terminal);
            out->terminal_token.assign(terminal_token.data(), terminal_token.size());

            /* Canonicalise the owner slot to the resolved token text: the owner
             * Schema declares steps as a String, so a legacy uint is rewritten to
             * its equivalent token before the bind runs (the token literal has
             * static lifetime).
             *
             * M5 note: this is NOT the write-back the queue path dropped. `steps`
             * is a key the CALLER already sent (a pre-v3 numeric id) and the bind
             * needs its String form; the queue path, by contrast, has no such key
             * and must not create one. The M5 shape therefore stays `route` +
             * `queue` for every document the App emits. */
            steps_entry->value = Value{};
            steps_entry->value.present = true;
            steps_entry->value.is_text = true;
            steps_entry->value.text = spec->token;
            return 0;
        }
    } // namespace

    bool looks_like_glkv3(std::string_view document) noexcept {
        if (document.empty()) return false;
        const auto first = static_cast<uint8_t>(document.front());
        return (first & 0xF0u) == 0x80u || first == 0xDEu || first == 0xDFu;
    }

    int32_t frame_v3(std::string_view document, profile::Document *out) {
        if (out == nullptr) return -1;

        profile::glkv3::Document decoded;
        const profile::glkv3::DecodeStatus status =
                profile::glkv3::decode_neutral(document, decoded,
                                               profile::glkv3::DecodeMode::Production);
        if (!status.ok()) return -1;
        if (!decoded.has_release || !decoded.has_terminal || !decoded.has_backend) {
            return -1;
        }

        out->release.assign(decoded.release.data(), decoded.release.size());
        out->terminal_token.assign(decoded.terminal.data(), decoded.terminal.size());
        out->backend_token.assign(decoded.backend.data(), decoded.backend.size());
        /* No route is selected yet; resolve_combination installs the token's
         * declared route (None for a backend without a route axis). */
        out->middleware = profile::kRouteNone;

        /* HOCON refactor root scalars: the wire carries them in the document
         * root (never inside a section), and the owner bind reads them through
         * the neutral root section (document.hpp kRootSection). Only a key the
         * document actually carried is materialised: an absent scalar has no
         * entry and no sentinel. */
        if (decoded.has_kernel_major || decoded.has_kernel_minor ||
            decoded.has_safe_mode) {
            Section &root = out->append_section(kRootSection);
            if (decoded.has_kernel_major) {
                root.add("kernel_major", decoded.kernel_major);
            }
            if (decoded.has_kernel_minor) {
                root.add("kernel_minor", decoded.kernel_minor);
            }
            if (decoded.has_safe_mode) {
                root.add("safe_mode", decoded.safe_mode ? uint64_t{1} : uint64_t{0});
            }
        }

        for (const profile::glkv3::Section &section : decoded.sections) {
            if (!known_owner_section(section.name)) return -1;
            profile::Section &target = out->append_section(section.name);
            for (const profile::glkv3::Entry &entry : section.entries) {
                /* S4 R4: preserve the wire type. Numeric slots become raw
                 * values; a str becomes a non-owning text view into the same
                 * decode buffer (frame_v3's caller owns it). bin/array are not
                 * part of the owner section vocabulary and fail closed. */
                switch (entry.value.type) {
                    /* S4 P1: Union is a manifest-level declaration for the
                     * dynamic plugin keys; a concrete value always carries its
                     * scalar kind, so a Union here is rejected. */
                    case profile::glkv3::WireType::Union:
                        return -1;
                    case profile::glkv3::WireType::UInt:
                        target.add(entry.key, entry.value.uint_value);
                        break;
                    case profile::glkv3::WireType::Int:
                        target.add(entry.key,
                                   static_cast<uint64_t>(entry.value.int_value));
                        break;
                    case profile::glkv3::WireType::Bool:
                        target.add(entry.key,
                                   entry.value.bool_value ? uint64_t{1} : uint64_t{0});
                        break;
                    case profile::glkv3::WireType::Str:
                        target.add_text(entry.key, entry.value.bytes);
                        break;
                    case profile::glkv3::WireType::Bin:
                    case profile::glkv3::WireType::Map:
                        return -1;
                    case profile::glkv3::WireType::Array: {
                        /* M2 declaration-driven composite: only a declared
                         * array field is admitted (design doc 5.0), and the
                         * materialisation is bounded and fail-closed. */
                        if (!profile::declared_array_field(section.name, entry.key)) {
                            return -1;
                        }
                        std::vector<profile::CompositeItem> items;
                        if (!materialise_array(entry.value, items)) return -1;
                        target.add_array(entry.key, std::move(items));
                        break;
                    }
                }
            }
        }
//         /* S4 payload owner (contract-design 3.15): the flat section is validated
//          * fail-closed BEFORE combination resolution. Without a payload section
//          * nothing changes -- no payload key is ever invented here. */
//         const profile::Section *payload_section = nullptr;
//         for (const profile::Section &section : out->sections) {
//             if (section.name == "payload") {
//                 payload_section = &section;
//                 break;
//             }
//         }
//         if (payload_section != nullptr && !validate_payload_section(*payload_section)) {
//             return -1;
//         }
        return resolve_combination(out, decoded);
    }
} // namespace ghostlock::profile
