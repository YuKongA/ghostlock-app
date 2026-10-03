/* Host test for the production profile entry dispatch (profile/entry.cpp,
 * GLKv3-4). It writes a GLKv3 document and a v2 document to disk and reads them
 * back through the same read_glk1_file() the --load-prebuilt-profile and
 * --ghostlock-app-call paths use, so the map-root probe, the MPack decode and
 * the v2 fallback are exercised together. A map-rooted document that is not
 * valid GLKv3 must fail closed instead of being retried as v2.
 *
 * The stdin variants share decode(), so covering the file entry covers the
 * auto-detection. */

#include "profile/entry.h"

#include "profile/binary.h"
#include "profile/glkv3.hpp"

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
    char release[64] = {0};
    ghostlock::profile::kernel_offsets parsed = {};
    ghostlock::binary_profile::component_ids ids = {};

    /* ---- v3 document: map root, schema 3, token selection. ---- */
    {
        const std::string path = temp_path("v3");
        const std::string bytes = ghostlock::profile::glkv3::encode(make_v3());
        assert(write_file(path, bytes));
        assert(ghostlock::profile_entry::read_glk1_file(
                       path.c_str(), &parsed, release, sizeof(release), &ids) == 0);
        assert(std::strcmp(release, "6.6.77-entry-test") == 0);
        assert(parsed.route == ghostlock::profile::kRouteSelectStack);
        assert(parsed.meta.kernel_major == 6);
        assert(parsed.offsets.init_task == 0x20dc000);
        assert(ids.terminal == ghostlock::binary_profile::kTerminalRootChild);
        assert(ids.backend == ghostlock::binary_profile::kBackendCve202643499);
        assert(ids.steps == 2);
        unlink(path.c_str());
    }

    /* ---- v2 document: magic root, object sections. ---- */
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
        parsed = {};
        assert(ghostlock::profile_entry::read_glk1_file(
                       path.c_str(), &parsed, release, sizeof(release), &ids) == 0);
        assert(std::strcmp(release, "6.6.77-entry-test") == 0);
        assert(parsed.route == ghostlock::profile::kRouteSelectStack);
        assert(parsed.offsets.init_task == 0x20dc000);
        unlink(path.c_str());
    }

    /* ---- A map root that is not valid GLKv3 fails closed. ---- */
    {
        const std::string path = temp_path("bad");
        /* map { "schema": 2 } - a valid MessagePack map with a wrong schema. */
        const std::string bytes = std::string("\x81\xa6schema\x02", 9);
        assert(write_file(path, bytes));
        assert(ghostlock::profile_entry::read_glk1_file(
                       path.c_str(), &parsed, release, sizeof(release), &ids) == -1);
        unlink(path.c_str());
    }

    std::puts("profile_entry_test: OK");
    return 0;
}
