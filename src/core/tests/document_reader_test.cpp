/** @file tests/document_reader_test.cpp
 *  A6-4 evidence: reading a GLKv3 document and its payload family.
 *
 *  Documents are built here with the same vendored library that reads them, so
 *  the test states what a writer produces rather than a hand-made byte string.
 *  Sections: 1. a well-formed document parses and its payload reads back whole,
 *  digest included; 2. a document this build does not read is refused before
 *  anything is looked up; 3. an entry that is absent, inadmissible or incomplete
 *  is refused with the payload path's own name.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "mpack.h"
#include "profile/glkv3_document.hpp"
#include "support/sha256.hpp"

namespace {
    using ghostlock::profile::Document;
    using ghostlock::profile::document_verdict_name;
    using ghostlock::profile::DocumentVerdict;
    using ghostlock::profile::kDocumentSchema;
    using ghostlock::profile::PayloadEntry;
    using ghostlock::support::sha256;
    using ghostlock::support::sha256_hex;

    constexpr const char *kBackend = "cve_2026_43499";
    constexpr const char *kName = "root_child_script";
    const std::uint8_t kPayload[] = {0xDEU, 0xADU, 0xBEU, 0xEFU, 0x01U, 0x02U};

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void write_entry(mpack_writer_t &writer, std::uint64_t declared_size,
                     const char *kind) {
        char hex[65];
        sha256_hex(sha256(kPayload, sizeof(kPayload)), hex);
        mpack_start_map(&writer, 4U);
        mpack_write_cstr(&writer, "bin");
        mpack_write_bin(&writer, reinterpret_cast<const char *>(kPayload),
                        static_cast<std::uint32_t>(sizeof(kPayload)));
        mpack_write_cstr(&writer, "sha256");
        mpack_write_cstr(&writer, hex);
        mpack_write_cstr(&writer, "size");
        mpack_write_uint(&writer, declared_size);
        mpack_write_cstr(&writer, "kind");
        mpack_write_cstr(&writer, kind);
        mpack_finish_map(&writer);
    }

    /** Build a document carrying one payload; bytes are owned by the caller. */
    char *build(std::uint64_t schema, const char *name, std::uint64_t declared_size,
                const char *kind, std::size_t *out_size) {
        char *data = nullptr;
        std::size_t size = 0U;
        mpack_writer_t writer;
        mpack_writer_init_growable(&writer, &data, &size);
        mpack_start_map(&writer, 2U);
        mpack_write_cstr(&writer, "schema");
        mpack_write_uint(&writer, schema);
        mpack_write_cstr(&writer, "backend");
        mpack_start_map(&writer, 1U);
        mpack_write_cstr(&writer, kBackend);
        mpack_start_map(&writer, 1U);
        mpack_write_cstr(&writer, "payload");
        mpack_start_map(&writer, name == nullptr ? 0U : 1U);
        if (name != nullptr) {
            mpack_write_cstr(&writer, name);
            write_entry(writer, declared_size, kind);
        }
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        mpack_finish_map(&writer);
        expect(mpack_writer_destroy(&writer) == mpack_ok, "the document was written");
        *out_size = size;
        return data;
    }

    DocumentVerdict read(const char *data, std::size_t size, Document &document) {
        return document.parse(reinterpret_cast<const std::uint8_t *>(data), size);
    }

    /* ---- 1: a well-formed document reads back whole ---- */
    void a_document_reads() {
        std::size_t size = 0U;
        char *data = build(kDocumentSchema, kName, sizeof(kPayload), "script", &size);
        Document document;
        expect(read(data, size, document) == DocumentVerdict::Ok, "a valid document parses");
        expect(document.schema() == kDocumentSchema, "the schema is the one version");
        PayloadEntry entry;
        expect(document.payload(std::string_view(kBackend), std::string_view(kName), entry) ==
                   DocumentVerdict::Ok,
               "its payload reads");
        expect(entry.size == sizeof(kPayload) && entry.bytes != nullptr,
               "the payload's bytes come back with their length");
        expect(entry.bytes != nullptr && std::memcmp(entry.bytes, kPayload, sizeof(kPayload)) == 0,
               "and they are the bytes that were written");
        expect(std::strcmp(entry.kind, "script") == 0, "the kind comes back");
        char recomputed[65];
        sha256_hex(sha256(entry.bytes, entry.size), recomputed);
        expect(std::strcmp(entry.sha256, recomputed) == 0,
               "the declared digest is the digest of those bytes");
        document.reset();
        MPACK_FREE(data);
    }

    /* ---- 2: what this build does not read is refused first ---- */
    void other_documents_are_refused() {
        for (const std::uint64_t schema : {0U, 1U, 2U, 4U, 99U}) {
            std::size_t size = 0U;
            char *data = build(schema, kName, sizeof(kPayload), "script", &size);
            Document document;
            expect(read(data, size, document) == DocumentVerdict::Invalid,
                   "a document of another schema is refused");
            expect(std::string_view(document_verdict_name(DocumentVerdict::Invalid)) ==
                       std::string_view("document-invalid"),
                   "the refusal is named document-invalid");
            PayloadEntry entry;
            expect(document.payload(std::string_view(kBackend), std::string_view(kName), entry) ==
                       DocumentVerdict::Invalid,
                   "and nothing is looked up in it afterwards");
            MPACK_FREE(data);
        }
        const std::uint8_t garbage[] = {0xC1U, 0x00U, 0x7FU, 0x7FU};
        Document broken;
        expect(broken.parse(garbage, sizeof(garbage)) == DocumentVerdict::Invalid,
               "bytes that are not a document are refused");
        expect(broken.parse(nullptr, 0U) == DocumentVerdict::Invalid, "no bytes are refused");
        PayloadEntry entry;
        expect(broken.payload(std::string_view(kBackend), std::string_view(kName), entry) ==
                   DocumentVerdict::Invalid,
               "an unparsed document refuses a read");
    }

    /* ---- 3: absent, inadmissible and incomplete entries ---- */
    void payload_refusals_are_named() {
        PayloadEntry entry;
        for (const char *name : {"free_name", "root_child", "shizuku_script", "root_child_"}) {
            std::size_t size = 0U;
            char *data = build(kDocumentSchema, name, sizeof(kPayload), "script", &size);
            Document document;
            expect(read(data, size, document) == DocumentVerdict::Ok, "the document itself is fine");
            const DocumentVerdict verdict =
                document.payload(std::string_view(kBackend), std::string_view(name), entry);
            expect(verdict == DocumentVerdict::Missing,
                   "a name the rule does not admit is refused");
            expect(std::string_view(document_verdict_name(verdict)) ==
                       std::string_view("payload-missing"),
                   "and it is named payload-missing");
            MPACK_FREE(data);
        }
        {
            std::size_t size = 0U;
            char *data = build(kDocumentSchema, nullptr, 0U, "script", &size);
            Document document;
            read(data, size, document);
            expect(document.payload(std::string_view(kBackend), std::string_view(kName), entry) ==
                       DocumentVerdict::Missing,
                   "an absent payload is refused");
            MPACK_FREE(data);
        }
        {
            std::size_t size = 0U;
            char *data = build(kDocumentSchema, kName, sizeof(kPayload) + 1U, "script", &size);
            Document document;
            read(data, size, document);
            expect(document.payload(std::string_view(kBackend), std::string_view(kName), entry) ==
                       DocumentVerdict::Missing,
                   "a declared size that disagrees with the bytes is refused");
            MPACK_FREE(data);
        }
        for (const char *kind : {"binary", "SCRIPT", ""}) {
            std::size_t size = 0U;
            char *data = build(kDocumentSchema, kName, sizeof(kPayload), kind, &size);
            Document document;
            read(data, size, document);
            expect(document.payload(std::string_view(kBackend), std::string_view(kName), entry) ==
                       DocumentVerdict::Missing,
                   "an undeclared kind is refused");
            MPACK_FREE(data);
        }
        {
            std::size_t size = 0U;
            char *data = build(kDocumentSchema, kName, sizeof(kPayload), "script", &size);
            Document document;
            read(data, size, document);
            expect(document.payload(std::string_view("another_backend"), std::string_view(kName),
                                    entry) == DocumentVerdict::Missing,
                   "a payload under another backend is not found here");
            expect(document.payload(std::string_view(kBackend),
                                    std::string_view("umh_forward_module"), entry) ==
                       DocumentVerdict::Missing,
                   "another admissible name is simply not present");
            MPACK_FREE(data);
        }
    }
}  // namespace

int main() {
    std::printf("document: a document reads\n");
    a_document_reads();
    std::printf("document: refusals before lookup\n");
    other_documents_are_refused();
    std::printf("document: payload refusals\n");
    payload_refusals_are_named();
    if (g_failures != 0U) {
        std::printf("document: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("document: PASS (schema first + payload family read + refusals named)\n");
    return 0;
}
