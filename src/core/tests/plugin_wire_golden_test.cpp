/* S4 P1 cross-language shape evidence (task-6 follow-up, Lead ruling 2025-10-05).
 *
 * The App encoder produces app/src/test/resources/plugin-wire-shape-golden.bin:
 * a real document with one enabled plugin (3 static fields + one params
 * override). This test is the SINGLE artefact both sides agree on:
 *
 *   (3) native parse + validate_plugin_wire ACCEPT it (id/fields checked);
 *   (4) the same bytes with the section key mutated to "plugin.demo" are
 *       REJECTED at decode time -- only the exact section "plugin" is canonical,
 *       so an owner-qualified section fails closed instead of being ignored.
 *
 * A control write of the untouched bytes proves the mutation is the only
 * difference (the surgery itself does not break the document). */

#include "plugin/wire.hpp"
#include "profile/entry.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace {
    const char *kGoldenPath = GLK_PLUGIN_GOLDEN;

    std::vector<std::uint8_t> read_bytes(const char *path) {
        std::ifstream file(path, std::ios::binary);
        assert(file.good());
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                         std::istreambuf_iterator<char>());
    }

    void write_bytes(const char *path, const std::vector<std::uint8_t> &bytes) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        assert(file.good());
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        assert(file.good());
    }

    /* MessagePack fixstr(6) "plugin": the root map section key. */
    const std::vector<std::uint8_t> kSectionKey = {0xa6u, 'p', 'l', 'u', 'g', 'i', 'n'};
    /* fixstr(11) "plugin.demo": the WRONG (owner-qualified) section shape. */
    const std::vector<std::uint8_t> kWrongSectionKey = {
            0xabu, 'p', 'l', 'u', 'g', 'i', 'n', '.', 'd', 'e', 'm', 'o'};

    bool replace_first(std::vector<std::uint8_t> &bytes,
                       const std::vector<std::uint8_t> &from,
                       const std::vector<std::uint8_t> &to) {
        for (std::size_t i = 0u; i + from.size() <= bytes.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0u; j < from.size(); ++j) {
                if (bytes[i + j] != from[j]) {
                    match = false;
                    break;
                }
            }
            if (!match) continue;
            const auto at = static_cast<std::ptrdiff_t>(i);
            bytes.erase(bytes.begin() + at, bytes.begin() + at +
                        static_cast<std::ptrdiff_t>(from.size()));
            bytes.insert(bytes.begin() + at, to.begin(), to.end());
            return true;
        }
        return false;
    }
} // namespace

int main() {
    using ghostlock::plugin::kMaxPluginsPerDocument;
    using ghostlock::plugin::PluginWireEntry;
    using ghostlock::plugin::PluginWireError;
    using ghostlock::plugin::validate_plugin_wire;

    /* (3) the App-encoded golden document is accepted end to end. */
    const auto golden = ghostlock::profile_entry::read_glk1_file(kGoldenPath);
    assert(golden.error == 0);
    PluginWireEntry entries[kMaxPluginsPerDocument]{};
    const auto wire =
            validate_plugin_wire(golden.document, entries, kMaxPluginsPerDocument);
    if (wire.error != PluginWireError::None) {
        std::fprintf(stderr, "golden rejected: %s id=%.*s accepted=%zu\n",
                     ghostlock::plugin::plugin_wire_error_name(wire.error),
                     static_cast<int>(wire.id.size()), wire.id.data(),
                     wire.accepted);
    }
    assert(wire.error == PluginWireError::None);
    assert(wire.accepted == 1u);
    assert(entries[0].id == "demo.plugin");
    assert(!entries[0].stage.empty());
    assert(entries[0].module_hash.size() == 64u);
    assert(entries[0].param_count >= 1u);

    /* Control: re-writing the untouched bytes keeps the document parseable, so
     * the mutation below is the only difference. */
    const std::vector<std::uint8_t> original = read_bytes(kGoldenPath);
    assert(!original.empty());
    write_bytes("/tmp/plugin-wire-golden-control.bin", original);
    const auto control = ghostlock::profile_entry::read_glk1_file(
            "/tmp/plugin-wire-golden-control.bin");
    assert(control.error == 0);

    /* (4) owner-qualified section shape -> rejected at decode time. */
    std::vector<std::uint8_t> mutated = original;
    assert(replace_first(mutated, kSectionKey, kWrongSectionKey));
    write_bytes("/tmp/plugin-wire-golden-wrong-shape.bin", mutated);
    const auto wrong = ghostlock::profile_entry::read_glk1_file(
            "/tmp/plugin-wire-golden-wrong-shape.bin");
    assert(wrong.error != 0);

    std::puts("plugin_wire_golden_test: ok");
    return 0;
}
