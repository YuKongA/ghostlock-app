/* S4 P1 step 3a gate tool: build and verify the GLKv3 documents the device
 * gate feeds to native over --ghostlock-app-call.
 *
 * It is a TOOL, not a test: it is not part of NATIVE_HOST_TESTS and is built on
 * demand with "make -C src plugin-gate-doc".
 *
 * Why it exists: the App owns the authoritative document, but a device gate
 * still needs (a) the same document with a plugin section added or removed and
 * (b) a host-side proof that native would accept it BEFORE pushing it to the
 * device. Both use the production codec (profile::glkv3::encode/decode_neutral)
 * and the production gates (profile::frame_v3 + plugin::validate_plugin_wire),
 * so the tool cannot drift from the runtime it feeds.
 *
 * Usage:
 *   plugin_gate_doc --verify <doc.bin>
 *   plugin_gate_doc --base <doc.bin> [--plugin <id> <stage> <module_path> <sha256>]
 *                   [--no-plugin] [--frame <frame.bin>] [--stdin-payload]
 *                   --out <file>
 *
 *   --verify        decode + wire-validate an existing document (exit 0/1)
 *   --base          a GLKv3 document to start from (framing decode; unknown
 *                   sections are preserved, the owner bind rejects them later)
 *   --plugin        add/replace the flattened plugin section
 *                   ("<id>.enabled" bool, ".stage"/".module_path"/".module_hash")
 *   --no-plugin     remove any plugin section
 *   --frame         append this frame chunk verbatim (it already carries its
 *                   own 4-byte length, exactly what the App sends after the doc)
 *   --stdin-payload emit [4-byte big-endian doc length][doc][frame], which is
 *                   what --ghostlock-app-call --enable-status-record reads;
 *                   the default is the bare document
 *   --out           output path ("-" = stdout)
 *
 * Every build re-decodes its own output and asserts frame_v3 + the plugin wire
 * gate accept it; a rejection exits non-zero and writes nothing. */

#include "plugin/wire.hpp"
#include "profile/glkv3.hpp"
#include "profile/glkv3_parse.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    using ghostlock::profile::glkv3::Document;
    using ghostlock::profile::glkv3::Entry;
    using ghostlock::profile::glkv3::Section;
    using ghostlock::profile::glkv3::Value;
    using ghostlock::profile::glkv3::WireType;

    int usage() {
        std::fprintf(stderr,
                     "usage: plugin_gate_doc --verify <doc.bin>\n"
                     "       plugin_gate_doc --base <doc.bin> [--plugin <id> <stage> "
                     "<path> <sha256>] [--no-plugin] [--frame <frame.bin>] "
                     "[--stdin-payload] --out <file>\n");
        return 2;
    }

    bool read_file(const char *path, std::string *out) {
        std::FILE *file = std::fopen(path, "rb");
        if (file == nullptr) return false;
        char buffer[4096];
        std::size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
            out->append(buffer, got);
        }
        std::fclose(file);
        return !out->empty();
    }

    bool write_file(const char *path, std::string_view bytes) {
        if (std::strcmp(path, "-") == 0) {
            return std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size();
        }
        std::FILE *file = std::fopen(path, "wb");
        if (file == nullptr) return false;
        const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        std::fclose(file);
        return ok;
    }

    /* The plugin section is the ONE flattened "<id>.<field>" owner section.
     * Entry keys are string_views, so the four composed "<id>.<field>" strings
     * live in a caller-owned fixed array: its element addresses never move, so
     * the views stay valid until encode() has read them. */
    void set_plugin_section(Document &doc, std::string_view id, std::string_view stage,
                            std::string_view module_path, std::string_view sha256,
                            std::array<std::string, 4> &key_storage) {
        doc.sections.push_back(Section{"plugin", {}});
        Section &section = doc.sections.back();
        const std::string prefix(id);
        std::size_t next = 0u;
        const auto add_key = [&key_storage, &next](const std::string &key) -> std::string_view {
            key_storage[next] = key;
            return key_storage[next++];
        };
        const auto add_str = [&section, &add_key](const std::string &key,
                                                  std::string_view text) {
            Value value{};
            value.type = WireType::Str;
            value.bytes = text;
            section.entries.push_back(Entry{add_key(key), value});
        };
        Value enabled{};
        enabled.type = WireType::Bool;
        enabled.bool_value = true;
        section.entries.push_back(Entry{add_key(prefix + ".enabled"), enabled});
        add_str(prefix + ".stage", stage);
        add_str(prefix + ".module_path", module_path);
        add_str(prefix + ".module_hash", sha256);
    }

    /* Removes one "<section>.<key>" (e.g. a stale profile token the schema
     * would otherwise keep); returns false when it was not present. */
    bool drop_key(Document &doc, std::string_view section_name, std::string_view key) {
        for (Section &section : doc.sections) {
            if (section.name != section_name) continue;
            for (std::size_t i = 0; i < section.entries.size(); ++i) {
                if (section.entries[i].key != key) continue;
                section.entries.erase(section.entries.begin() +
                                      static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    bool strip_plugin_section(Document &doc) {
        const std::size_t before = doc.sections.size();
        std::vector<Section> kept;
        kept.reserve(doc.sections.size());
        for (Section &section : doc.sections) {
            if (section.name != "plugin") kept.push_back(section);
        }
        doc.sections = std::move(kept);
        return doc.sections.size() != before;
    }

    /* The pre-push proof: native's own framing decode plus the P1 wire gate. */
    bool verify_document(std::string_view bytes, bool print) {
        if (!ghostlock::profile::looks_like_glkv3(bytes)) {
            std::fprintf(stderr, "verify: not a GLKv3 map root\n");
            return false;
        }
        ghostlock::profile::Document parsed{};
        if (ghostlock::profile::frame_v3(bytes, &parsed) != 0) {
            std::fprintf(stderr, "verify: frame_v3 rejected the document\n");
            return false;
        }
        ghostlock::plugin::PluginWireEntry entries[ghostlock::plugin::kMaxPluginsPerDocument]{};
        const auto wire = ghostlock::plugin::validate_plugin_wire(
                parsed, entries, ghostlock::plugin::kMaxPluginsPerDocument);
        if (wire.error != ghostlock::plugin::PluginWireError::None) {
            std::fprintf(stderr, "verify: plugin wire rejected: %s\n",
                         ghostlock::plugin::plugin_wire_error_name(wire.error));
            return false;
        }
        if (print) {
            std::fprintf(stderr, "verify ok: %zu bytes, %zu sections, %zu plugin entries\n",
                         bytes.size(), parsed.sections.size(), wire.accepted);
            for (std::size_t i = 0; i < wire.accepted; ++i) {
                std::fprintf(stderr, "  plugin %.*s stage=%.*s path=%.*s\n",
                             static_cast<int>(entries[i].id.size()), entries[i].id.data(),
                             static_cast<int>(entries[i].stage.size()),
                             entries[i].stage.data(),
                             static_cast<int>(entries[i].module_path.size()),
                             entries[i].module_path.data());
            }
        }
        return true;
    }
} // namespace

int main(int argc, char **argv) {
    const char *base_path = nullptr;
    const char *verify_path = nullptr;
    const char *frame_path = nullptr;
    const char *out_path = nullptr;
    const char *plugin_id = nullptr;
    const char *plugin_stage = nullptr;
    const char *plugin_path = nullptr;
    const char *plugin_sha = nullptr;
    bool strip_plugin = false;
    bool stdin_payload = false;
    std::vector<std::pair<std::string, std::string>> drops;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        const auto next = [&](const char *what) -> const char * {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "plugin_gate_doc: %s needs a value\n", what);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--verify") {
            verify_path = next("--verify");
        } else if (arg == "--base") {
            base_path = next("--base");
        } else if (arg == "--frame") {
            frame_path = next("--frame");
        } else if (arg == "--out") {
            out_path = next("--out");
        } else if (arg == "--no-plugin") {
            strip_plugin = true;
        } else if (arg == "--stdin-payload") {
            stdin_payload = true;
        } else if (arg == "--drop") {
            /* --drop <section> <key>: remove one token from the base document
             * (a schema-derived default then applies instead of a stale value). */
            if (i + 2 >= argc) return usage();
            const char *drop_section = argv[++i];
            const char *drop_key_name = argv[++i];
            drops.emplace_back(drop_section, drop_key_name);
        } else if (arg == "--plugin") {
            if (i + 4 >= argc) return usage();
            plugin_id = argv[++i];
            plugin_stage = argv[++i];
            plugin_path = argv[++i];
            plugin_sha = argv[++i];
        } else {
            return usage();
        }
    }

    if (verify_path != nullptr) {
        std::string bytes;
        if (!read_file(verify_path, &bytes)) {
            std::fprintf(stderr, "plugin_gate_doc: cannot read %s\n", verify_path);
            return 1;
        }
        return verify_document(bytes, true) ? 0 : 1;
    }

    if (base_path == nullptr || out_path == nullptr) return usage();

    std::string document;
    if (!read_file(base_path, &document)) {
        std::fprintf(stderr, "plugin_gate_doc: cannot read %s\n", base_path);
        return 1;
    }
    /* decode_neutral keeps unknown sections/keys (the owner bind rejects them
     * later), which is what lets a tool mutate a document it did not author. */
    const std::string_view view(document.data(), document.size());
    if (!ghostlock::profile::looks_like_glkv3(view)) {
        std::fprintf(stderr, "plugin_gate_doc: %s is not a GLKv3 map root\n", base_path);
        return 1;
    }
    Document decoded{};
    /* Entry keys are views; the composed plugin keys must outlive encode() and
     * their addresses must not move, hence the fixed array. */
    std::array<std::string, 4> plugin_keys{};
    const auto status = ghostlock::profile::glkv3::decode_neutral(view, decoded);
    if (!status.ok()) {
        std::fprintf(stderr, "plugin_gate_doc: decode failed: %s\n",
                     std::string(ghostlock::profile::glkv3::decode_code_name(status.code))
                             .c_str());
        return 1;
    }
    for (const auto &[section_name, key] : drops) {
        if (!drop_key(decoded, section_name, key)) {
            std::fprintf(stderr, "plugin_gate_doc: --drop %s %s: not present\n",
                         section_name.c_str(), key.c_str());
        }
    }
    if (strip_plugin) {
        (void)strip_plugin_section(decoded);
    }
    if (plugin_id != nullptr) {
        (void)strip_plugin_section(decoded); /* replace, never duplicate */
        set_plugin_section(decoded, plugin_id, plugin_stage, plugin_path, plugin_sha,
                           plugin_keys);
    }

    const std::string encoded = ghostlock::profile::glkv3::encode(decoded);
    if (encoded.empty()) {
        std::fprintf(stderr, "plugin_gate_doc: encode failed\n");
        return 1;
    }
    if (!verify_document(encoded, true)) {
        return 1; /* never write a document native would refuse */
    }

    std::string frame;
    if (frame_path != nullptr && !read_file(frame_path, &frame)) {
        std::fprintf(stderr, "plugin_gate_doc: cannot read %s\n", frame_path);
        return 1;
    }

    std::string payload;
    if (stdin_payload) {
        const std::uint32_t length = static_cast<std::uint32_t>(encoded.size());
        payload.push_back(static_cast<char>((length >> 24u) & 0xffu));
        payload.push_back(static_cast<char>((length >> 16u) & 0xffu));
        payload.push_back(static_cast<char>((length >> 8u) & 0xffu));
        payload.push_back(static_cast<char>(length & 0xffu));
    }
    payload.append(encoded);
    payload.append(frame);
    if (!write_file(out_path, payload)) {
        std::fprintf(stderr, "plugin_gate_doc: cannot write %s\n", out_path);
        return 1;
    }
    std::fprintf(stderr, "plugin_gate_doc: wrote %zu bytes to %s (doc %zu)\n",
                 payload.size(), out_path, encoded.size());
    return 0;
}
