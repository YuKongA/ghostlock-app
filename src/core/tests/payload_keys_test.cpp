/** @file tests/payload_keys_test.cpp
 *  A6-4 evidence: the payload family's keys and its name rule.
 *
 *  Sections: 1. the four keys and the three kind values are the declared ones;
 *  2. a name is a selectable terminal plus a required suffix, and nothing else
 *  passes; 3. a key path is built only for admissible parts.
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "profile/payload_keys.hpp"

namespace {
    using ghostlock::profile::kPayloadKeyCount;
    using ghostlock::profile::kPayloadKeys;
    using ghostlock::profile::kPayloadKindCount;
    using ghostlock::profile::kPayloadKinds;
    using ghostlock::profile::payload_key_is_declared;
    using ghostlock::profile::payload_kind_is_declared;
    using ghostlock::profile::payload_name_is_valid;
    using ghostlock::profile::payload_path;
    using ghostlock::profile::terminal_is_declared;
    using ghostlock::profile::terminal_of;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool name_ok(const char *name) { return payload_name_is_valid(std::string_view(name)); }

    /* ---- 1: the declared keys and kinds ---- */
    void declared_parts_hold() {
        expect(kPayloadKeyCount == 4U, "a payload carries four keys");
        expect(payload_key_is_declared(std::string_view("bin")) &&
                   payload_key_is_declared(std::string_view("sha256")) &&
                   payload_key_is_declared(std::string_view("size")) &&
                   payload_key_is_declared(std::string_view("kind")),
               "the four declared keys are accepted");
        expect(!payload_key_is_declared(std::string_view("data")), "an undeclared key is refused");
        expect(kPayloadKindCount == 3U, "a payload has three kinds");
        expect(payload_kind_is_declared(std::string_view("plan")) &&
                   payload_kind_is_declared(std::string_view("script")) &&
                   payload_kind_is_declared(std::string_view("module")),
               "the three kinds are accepted");
        expect(!payload_kind_is_declared(std::string_view("binary")), "an undeclared kind is refused");
    }

    /* ---- 2: the name rule ---- */
    void the_name_rule_holds() {
        expect(terminal_is_declared(std::string_view("root_child")), "root_child offers a terminal");
        expect(terminal_is_declared(std::string_view("umh_forward")), "umh_forward offers a terminal");
        expect(!terminal_is_declared(std::string_view("shizuku")),
               "a combination token is not a terminal");
        expect(name_ok("root_child_script"), "a terminal with a suffix is admissible");
        expect(name_ok("umh_forward_module"), "so is the other terminal");
        expect(!name_ok("root_child"), "a bare terminal is not admissible");
        expect(!name_ok("shizuku_script"), "a combination token cannot lead a name");
        expect(!name_ok("free_name"), "a name unrelated to a terminal is not admissible");
        expect(!name_ok("root_child_"), "an empty suffix is not admissible");
        expect(!name_ok("root_child_SCRIPT"), "an uppercase suffix is not admissible");
        expect(!name_ok("root_childx_script"), "a name that only looks like a terminal is refused");
        expect(!name_ok("root_child_0123456789abcdefg"), "an over-long suffix is refused");
        expect(!name_ok(""), "an empty name is refused");
        expect(std::string_view(terminal_of(std::string_view("umh_forward_module"))) ==
                   std::string_view("umh_forward"),
               "the terminal of a name is reported");
        expect(terminal_of(std::string_view("free_name")).empty(),
               "a name without a terminal reports none");
    }

    /* ---- 3: the path follows the name rule ---- */
    void the_path_is_built_or_refused() {
        char path[192];
        expect(payload_path(path, sizeof(path), std::string_view("cve_2026_43499"),
                            std::string_view("root_child_script"), std::string_view("sha256")),
               "a path is built for admissible parts");
        expect(std::strcmp(path, "backend.cve_2026_43499.payload.root_child_script.sha256") == 0,
               "the path has the declared shape");
        expect(!payload_path(path, sizeof(path), std::string_view("cve_2026_43499"),
                             std::string_view("free_name"), std::string_view("bin")),
               "an inadmissible name builds nothing");
        expect(path[0] == '\0', "and leaves nothing behind either");
        expect(!payload_path(path, sizeof(path), std::string_view("cve_2026_43499"),
                             std::string_view("root_child_script"), std::string_view("data")),
               "an undeclared key builds nothing");
        expect(!payload_path(path, sizeof(path), std::string_view(""),
                             std::string_view("root_child_script"), std::string_view("bin")),
               "no backend builds nothing");
        char tiny[16];
        expect(!payload_path(tiny, sizeof(tiny), std::string_view("cve_2026_43499"),
                             std::string_view("root_child_script"), std::string_view("bin")),
               "a path that does not fit is refused, not truncated");
        expect(tiny[0] == '\0', "and the buffer is left empty");
    }
}  // namespace

int main() {
    std::printf("payload keys: declared parts\n");
    declared_parts_hold();
    std::printf("payload keys: name rule\n");
    the_name_rule_holds();
    std::printf("payload keys: paths\n");
    the_path_is_built_or_refused();
    if (g_failures != 0U) {
        std::printf("payload keys: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("payload keys: PASS (four keys + three kinds + terminal-bound names + bounded paths)\n");
    return 0;
}
