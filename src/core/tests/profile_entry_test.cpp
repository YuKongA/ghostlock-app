/* Host test for the production profile entry dispatch (profile/entry.cpp, A2-5).
 * It writes a GLKv3 document to disk and reads it back through the same
 * read_glk1_file() the --load-prebuilt-profile and --ghostlock-app-call paths
 * use, so the map-root probe and the neutral framing are exercised together.
 * The result is a neutral profile::Document (no owner bound). Native is
 * GLKv3-only (S4 R2c): a map-rooted document that is not valid GLKv3 and any
 * non-map root (the v2 magic 0x21) fail closed instead of being retried as v2. */

#include "profile/entry.h"

#include "profile/document.hpp"
#include "profile/glkv3.hpp"
#include "contract/model.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace {
    using ghostlock::profile::glkv3::Document;
    using ghostlock::profile::glkv3::Entry;
    using ghostlock::profile::glkv3::Section;
    using ghostlock::profile::glkv3::Value;
    using ghostlock::profile::glkv3::WireType;

    Value uint_value(uint64_t value) {
        Value out;
        out.type = WireType::UInt;
        out.uint_value = value;
        return out;
    }

    void put(Document &doc, std::string_view name, std::string_view key, Value value) {
        Section *section = doc.find_section(name);
        if (section == nullptr) section = &doc.append_section(name);
        section->entries.push_back(Entry{key, std::move(value)});
    }

    Document make_v3() {
        Document doc;
        doc.schema = 3;
        doc.has_release = true;
        doc.release = "6.6.77-entry-test";
        doc.has_terminal = true;
        doc.terminal = "root_child";
        doc.has_backend = true;
        doc.backend = "cve_2026_43499";
        doc.has_route = true;
        doc.route = "select_stack";
        /* HOCON refactor: root scalars travel in the document root. */
        doc.has_kernel_major = true;
        doc.kernel_major = 6;
        put(doc, "backend.cve_2026_43499.abi.offset", "init_task", uint_value(0x20dc000));
        put(doc, "backend.cve_2026_43499", "steps", uint_value(2));
        return doc;
    }

    std::string temp_path(const char *tag) {
        std::string path = "/tmp/ghostlock-entry-";
        path += tag;
        path += "-";
        path += std::to_string(static_cast<long>(getpid()));
        path += ".bin";
        return path;
    }

    bool write_file(const std::string &path, const std::string &bytes) {
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) return false;
        const ssize_t written = write(fd, bytes.data(), bytes.size());
        close(fd);
        return written == static_cast<ssize_t>(bytes.size());
    }

// USER DIRECTIVE 2026-10-05: payload paused -> the payload-only test helpers
// (text_value / kSha / payload_accepts / payload_doc / dump_neutral /
//  kExpectedNoPayloadDump) are line-prefixed as well, so no unused-function
// warning is left behind. temp_path/write_file/put/make_v3 stay ACTIVE:
// the v3/v2/negative cases below still use them.
//     Value text_value(std::string_view text) {
//         Value out;
//         out.type = WireType::Str;
//         out.bytes = text;
//         return out;
//     }
// 
//     /* ---- S4 payload owner (contract-design 3.15): accept/reject matrix ---- */
// 
//     constexpr std::string_view kSha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
// 
//     /* Hand-builds a document with the given payload keys, writes it and reads it
//      * back through the production entry point (the same read_glk1_file the
//      * --load-prebuilt-profile / app-call paths use, where the payload validator
//      * runs). */
//     bool payload_accepts(const Document &doc, const char *tag) {
//         const std::string path = temp_path(tag);
//         const std::string bytes = ghostlock::profile::glkv3::encode(doc);
//         if (!write_file(path, bytes)) {
//             return false;
//         }
//         const ghostlock::profile_entry::ReadResult result =
//                 ghostlock::profile_entry::read_glk1_file(path.c_str());
//         (void)unlink(path.c_str());
//         return result.error == 0;
//     }
// 
//     Document payload_doc(std::string_view tier) {
//         Document doc = make_v3();
//         put(doc, "payload", "tier", text_value(tier));
//         return doc;
//     }
// 
//     /* Field/value sequence of the neutral document, for the no-payload
//      * regression: the payload validator must not invent, drop or reorder
//      * anything when no payload section exists. */
//     std::string dump_neutral(const ghostlock::profile::Document &doc) {
//         std::string out;
//         for (const ghostlock::profile::Section &section : doc.sections) {
//             for (const ghostlock::profile::Entry &entry : section.entries) {
//                 out += section.name;
//                 out += '.';
//                 out += entry.key;
//                 out += '=';
//                 out += entry.value.is_text ? std::string(entry.value.text)
//                                            : std::to_string(entry.value.raw);
//                 out += ';';
//             }
//         }
//         return out;
//     }
// 
// 
//     /* Frozen field/value sequence of a document WITHOUT a payload section: the
//      * payload validator must leave such a document byte-for-byte as it was
//      * (asserted through the production entry point, not a private helper). */
//     constexpr std::string_view kExpectedNoPayloadDump =
//             "backend.cve_2026_43499.steps=pselect_rootchild;common.kernel_major=6;"
//             "backend.cve_2026_43499.abi.offset.init_task=34455552;";
// 
// // USER DIRECTIVE 2026-10-05: payload paused -> the S4 payload owner cases are
// // commented out below (line-prefixed: the body contains its own block comments).
//     void test_payload_section() {
//         /* ---- positives: one minimal legal document per tier ---- */
//         {
//             Document doc = payload_doc("exec");
//             put(doc, "payload", "exec.command", text_value("tool --flag"));
//             put(doc, "payload", "exec.sha256", text_value(kSha));
//             assert(payload_accepts(doc, "pl-exec"));
//         }
//         {
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("bin/run.sh"));
//             put(doc, "payload", "script.sha256", text_value(kSha));
//             assert(payload_accepts(doc, "pl-script"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(2));
//             put(doc, "payload", "ko.0.path", text_value("helper.ko"));
//             put(doc, "payload", "ko.1.path", text_value("m2.ko"));
//             put(doc, "payload", "ko.1.sha256", text_value(kSha));
//             assert(payload_accepts(doc, "pl-ko"));
//         }
//         {
//             /* Boundary: ko.count == 8 with indices 0..7 (the schema maximum). */
//             static constexpr std::string_view kKeys[8] = {
//                     "ko.0.path", "ko.1.path", "ko.2.path", "ko.3.path",
//                     "ko.4.path", "ko.5.path", "ko.6.path", "ko.7.path"};
//             static constexpr std::string_view kPaths[8] = {
//                     "k0.ko", "k1.ko", "k2.ko", "k3.ko", "k4.ko", "k5.ko", "k6.ko", "k7.ko"};
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(8));
//             for (std::size_t i = 0u; i < 8u; ++i) {
//                 put(doc, "payload", kKeys[i], text_value(kPaths[i]));
//             }
//             assert(payload_accepts(doc, "pl-ko8"));
//         }
// 
//         /* ---- negatives: every rule is fail-closed ---- */
//         {
//             /* "root" is NOT a payload tier (exec/script/ko only): the manager
//              * selection travels on its own axis (handoff design). */
//             Document unknown_tier = payload_doc("root");
//             assert(!payload_accepts(unknown_tier, "pl-tier"));
//         }
//         {
//             Document missing = payload_doc("exec");
//             assert(!payload_accepts(missing, "pl-exec-nocmd"));
//         }
//         {
//             Document missing = payload_doc("script");
//             assert(!payload_accepts(missing, "pl-script-nopath"));
//         }
//         {
//             /* Cross-tier keys: exec may not carry script.path and vice versa. */
//             Document doc = payload_doc("exec");
//             put(doc, "payload", "exec.command", text_value("tool"));
//             put(doc, "payload", "script.path", text_value("bin/run.sh"));
//             assert(!payload_accepts(doc, "pl-exec-script"));
//         }
//         {
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("bin/run.sh"));
//             put(doc, "payload", "exec.command", text_value("tool"));
//             assert(!payload_accepts(doc, "pl-script-exec"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.0.path", text_value("a.ko"));
//             assert(!payload_accepts(doc, "pl-ko-nocount"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(0));
//             assert(!payload_accepts(doc, "pl-ko-zero"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(9));
//             put(doc, "payload", "ko.0.path", text_value("a.ko"));
//             assert(!payload_accepts(doc, "pl-ko-nine"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(1));
//             put(doc, "payload", "ko.8.path", text_value("a.ko"));
//             assert(!payload_accepts(doc, "pl-ko-index"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(2));
//             put(doc, "payload", "ko.0.path", text_value("a.ko"));
//             put(doc, "payload", "ko.0.path", text_value("b.ko"));
//             assert(!payload_accepts(doc, "pl-ko-dup"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(2));
//             put(doc, "payload", "ko.0.path", text_value("a.ko"));
//             assert(!payload_accepts(doc, "pl-ko-short"));
//         }
//         {
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(1));
//             const std::string long_path(257u, 'p');
//             put(doc, "payload", "ko.0.path", text_value(long_path));
//             assert(!payload_accepts(doc, "pl-ko-long"));
//         }
//         {
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("/etc/passwd"));
//             assert(!payload_accepts(doc, "pl-abs"));
//         }
//         {
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("a/../b"));
//             assert(!payload_accepts(doc, "pl-dotdot"));
//         }
//         {
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("a\\b"));
//             assert(!payload_accepts(doc, "pl-backslash"));
//         }
//         {
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("a\nb"));
//             assert(!payload_accepts(doc, "pl-control"));
//         }
//         {
//             const std::string short_hash(63u, 'a');
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("bin/run.sh"));
//             put(doc, "payload", "script.sha256", text_value(short_hash));
//             assert(!payload_accepts(doc, "pl-sha-short"));
//         }
//         {
//             const std::string upper_hash = [] {
//                 std::string out(64u, 'A');
//                 return out;
//             }();
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("bin/run.sh"));
//             put(doc, "payload", "script.sha256", text_value(upper_hash));
//             assert(!payload_accepts(doc, "pl-sha-upper"));
//         }
//         {
//             const std::string non_hex(64u, 'z');
//             Document doc = payload_doc("script");
//             put(doc, "payload", "script.path", text_value("bin/run.sh"));
//             put(doc, "payload", "script.sha256", text_value(non_hex));
//             assert(!payload_accepts(doc, "pl-sha-nonhex"));
//         }
//         {
//             /* Unknown key inside the owner section. */
//             Document doc = payload_doc("exec");
//             put(doc, "payload", "exec.command", text_value("tool"));
//             put(doc, "payload", "exec.bogus", text_value("x"));
//             assert(!payload_accepts(doc, "pl-unknown"));
//         }
//         {
//             /* The pre-r3 spelling "<i>.path" (no ko. node) is an unknown key and
//              * must stay rejected: the index lives under ko. beside ko.count. */
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(1));
//             put(doc, "payload", "0.path", text_value("a.ko"));
//             assert(!payload_accepts(doc, "pl-ko-old-spelling"));
//         }
//         {
//             /* ...and so is a foreign prefix instead of ko. */
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", uint_value(1));
//             put(doc, "payload", "mod.0.path", text_value("a.ko"));
//             assert(!payload_accepts(doc, "pl-ko-foreign-prefix"));
//         }
//         {
//             /* ko.count must be an integer, not text. */
//             Document doc = payload_doc("ko");
//             put(doc, "payload", "ko.count", text_value("2"));
//             assert(!payload_accepts(doc, "pl-ko-count-text"));
//         }
// 
//         /* ---- regression: a document WITHOUT a payload section is unchanged ---- */
//         {
//             const std::string path = temp_path("no-payload");
//             assert(write_file(path, ghostlock::profile::glkv3::encode(make_v3())));
//             const ghostlock::profile_entry::ReadResult result =
//                     ghostlock::profile_entry::read_glk1_file(path.c_str());
//             (void)unlink(path.c_str());
//             assert(result.error == 0);
//             std::printf("no-payload dump: %s\n", dump_neutral(result.document).c_str());
//             assert(dump_neutral(result.document) == kExpectedNoPayloadDump);
//         }
//         std::puts("profile_entry_test: payload_section ok");
//     }
// 
} // namespace

int main() {
    // USER DIRECTIVE 2026-10-05: payload paused
    // test_payload_section();

    /* ---- v3 document: map root, schema 3, token selection, neutral carry. ---- */
    {
        const std::string path = temp_path("v3");
        const std::string bytes = ghostlock::profile::glkv3::encode(make_v3());
        assert(write_file(path, bytes));
        const ghostlock::profile_entry::ReadResult result =
                ghostlock::profile_entry::read_glk1_file(path.c_str());
        assert(result.error == 0);
        const ghostlock::profile::Document &doc = result.document;
        assert(doc.release == "6.6.77-entry-test");
        assert(doc.middleware == ghostlock::profile::kRouteSelectStack);
        assert(doc.terminal_token == "root_child");
        assert(doc.backend_token == "cve_2026_43499");
        /* Root scalars land in the neutral root section (empty name). */
        const ghostlock::profile::Value *major =
                doc.find_value("", "kernel_major");
        assert(major != nullptr && major->raw == 6);
        const ghostlock::profile::Value *init =
                doc.find_value("backend.cve_2026_43499.abi.offset", "init_task");
        assert(init != nullptr && init->raw == 0x20dc000);
        const ghostlock::profile::Value *steps =
                doc.find_value("backend.cve_2026_43499", "steps");
        /* The legacy uint id 2 is rewritten to the equivalent token. */
        assert(steps != nullptr && steps->is_text);
        assert(steps->text == "pselect_rootchild");
        assert(doc.combination != 0);
        unlink(path.c_str());
    }

    /* ---- Negative: a v2 byte document is rejected, not retried or panicked. ---- */
    {
        /* A well-formed v2 container: little-endian magic 0x0D000721,
         * version 2, numeric ids, a release and an empty section list. Native
         * no longer has a v2 reader, so the whole document must be refused
         * (error == -1) rather than misparsed as GLKv3. */
        std::string bytes("\x21\x07\x00\x0d\x02\x00", 6);
        bytes += "\x01\x00"; /* terminal: root_child */
        bytes += "\x01\x00"; /* backend: cve_2026_43499 */
        bytes += "\x02\x00"; /* route: select_stack */
        const std::string release = "6.6.77-entry-test";
        bytes += static_cast<char>(release.size());
        bytes += '\0'; /* release_length high byte */
        bytes += '\0';
        bytes += '\0'; /* reserved */
        bytes += release;
        bytes += '\0';
        bytes += '\0'; /* section_count = 0 */

        const std::string path = temp_path("v2");
        assert(write_file(path, bytes));
        const ghostlock::profile_entry::ReadResult result =
                ghostlock::profile_entry::read_glk1_file(path.c_str());
        assert(result.error == -1);
        unlink(path.c_str());
    }

    /* ---- A map root that is not valid GLKv3 fails closed. ---- */
    {
        const std::string path = temp_path("bad");
        /* map { "schema": 2 } - a valid MessagePack map with a wrong schema. */
        const std::string bytes = std::string("\x81\xa6schema\x02", 9);
        assert(write_file(path, bytes));
        const ghostlock::profile_entry::ReadResult result =
                ghostlock::profile_entry::read_glk1_file(path.c_str());
        assert(result.error == -1);
        unlink(path.c_str());
    }

    std::puts("profile_entry_test: OK");
    return 0;
}
