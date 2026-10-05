/* Host test for the production profile entry dispatch (profile/entry.cpp, A2-5).
 * It writes a GLKv3 document and a v2 document to disk and reads them back
 * through the same read_glk1_file() the --load-prebuilt-profile and
 * --ghostlock-app-call paths use, so the map-root probe and the neutral framing
 * are exercised together. The result is a neutral profile::Document (no owner
 * bound); a map-rooted document that is not valid GLKv3 fails closed instead of
 * being retried as v2. */

#include "profile/entry.h"

#include "profile/binary.h"
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
        put(doc, "meta", "kernel_major", uint_value(6));
        put(doc, "offset", "init_task", uint_value(0x20dc000));
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
                doc.find_value("meta", "kernel_major");
        assert(major != nullptr && major->raw == 6);
        const ghostlock::profile::Value *init =
                doc.find_value("offset", "init_task");
        assert(init != nullptr && init->raw == 0x20dc000);
        const ghostlock::profile::Value *steps =
                doc.find_value("backend.cve_2026_43499", "steps");
        assert(steps != nullptr && steps->raw == 2);
        unlink(path.c_str());
    }

    /* ---- v2 document: magic root, object sections, numeric ids. ---- */
    {
        ghostlock::profile::kernel_offsets values = {};
        values.uname_r = "6.6.77-entry-test";
        values.route = ghostlock::profile::kRouteSelectStack;
        values.meta.kernel_major = 6;
        values.offsets.init_task = 0x20dc000;
        char buffer[8192];
        const int32_t size =
                ghostlock::binary_profile::serialize(&values, buffer, sizeof(buffer));
        assert(size > 0);
        const std::string path = temp_path("v2");
        assert(write_file(path, std::string(buffer, static_cast<size_t>(size))));
        const ghostlock::profile_entry::ReadResult result =
                ghostlock::profile_entry::read_glk1_file(path.c_str());
        assert(result.error == 0);
        const ghostlock::profile::Document &doc = result.document;
        assert(doc.release == "6.6.77-entry-test");
        assert(doc.middleware == ghostlock::profile::kRouteSelectStack);
        assert(doc.terminal == ghostlock::binary_profile::kTerminalRootChild);
        assert(doc.backend == ghostlock::binary_profile::kBackendCve202643499);
        const ghostlock::profile::Value *init =
                doc.find_value("offset", "init_task");
        assert(init != nullptr && init->raw == 0x20dc000);
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
