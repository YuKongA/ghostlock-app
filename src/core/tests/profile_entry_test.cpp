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
        put(doc, "common", "kernel_major", uint_value(6));
        put(doc, "platform.abi.offset", "init_task", uint_value(0x20dc000));
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
} // namespace

int main() {
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
        const ghostlock::profile::Value *major =
                doc.find_value("common", "kernel_major");
        assert(major != nullptr && major->raw == 6);
        const ghostlock::profile::Value *init =
                doc.find_value("platform.abi.offset", "init_task");
        assert(init != nullptr && init->raw == 0x20dc000);
        const ghostlock::profile::Value *steps =
                doc.find_value("backend.cve_2026_43499", "steps");
        assert(steps != nullptr && steps->raw == 2);
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
