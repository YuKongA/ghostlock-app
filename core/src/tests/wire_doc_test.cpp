/** @file tests/wire_doc_test.cpp
 *  The wire reader: framing, bounded JSON, typed reads, base64.
 *
 *  判据：解析一棵小树并逐项核对（kind/count/child/edge_at/u64_of），再核对四种具名拒绝。
 *  本测试同时是「JSON 树形状」的证据：每个对象/数组的子边成连续区间，直接子键在其中可查。
 */
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "script/wire_doc.hpp"

using namespace ghostlock;

namespace {

    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++g_failures; }
    }

    std::span<const std::byte> bytes_of(std::string_view text) noexcept {
        return std::span<const std::byte>(reinterpret_cast<const std::byte *>(text.data()), text.size());
    }

    void a_small_tree_is_reachable() {
        constexpr std::string_view kJson =
                "{\"schema\": \"GLKv3_wire\", \"step\": {\"key\": \"k\", \"backend\": \"b\"}, "
                "\"budgets\": {\"max_instructions\": 200000}, \"backend\": {\"x\": 1, \"nested\": {\"y\": \"0x40\"}, \"arr\": [1, 2]}}";
        script::WireDoc doc{};
        const char *refusal = nullptr;
        expect(doc.parse(bytes_of(kJson), &refusal), "a small document parses");
        const script::JsonNode *root = doc.root();
        expect(root != nullptr && root->kind == script::JsonNode::Kind::Obj, "the root is an object");
        const script::JsonNode *backend = doc.find("backend");
        std::printf("backend: %s kind=%u count=%u first=%u\n", backend == nullptr ? "null" : "found",
                    backend == nullptr ? 9U : static_cast<unsigned>(backend->kind),
                    backend == nullptr ? 9U : backend->count, backend == nullptr ? 9U : backend->first);
        expect(backend != nullptr && backend->kind == script::JsonNode::Kind::Obj, "backend is an object");
        if (backend != nullptr) {
            for (std::uint32_t i = 0U; i < backend->count; ++i) {
                const auto *edge = doc.edge_at(*backend, i);
                std::printf("  edge %u: key=%.*s\n", i, static_cast<int>(edge == nullptr ? 0U : edge->key.size()),
                            edge == nullptr ? "" : edge->key.data());
            }
            const script::JsonNode *x = doc.child(*backend, "x");
            std::printf("  child(x): %s kind=%u\n", x == nullptr ? "null" : "found",
                        x == nullptr ? 9U : static_cast<unsigned>(x->kind));
            std::uint64_t value = 0U;
            expect(x != nullptr && doc.u64_of(*x, value) && value == 1U, "x reads as 1");
            const script::JsonNode *y = doc.find("backend.nested.y");
            expect(y != nullptr && doc.u64_of(*y, value) && value == 0x40U, "a 0x string reads as 64");
            const script::JsonNode *arr = doc.child(*backend, "arr");
            expect(arr != nullptr && arr->kind == script::JsonNode::Kind::Arr && arr->count == 2U,
                   "the array holds two members");
            const script::JsonNode *second = arr == nullptr ? nullptr : doc.element(*arr, 1U);
            expect(second != nullptr && doc.u64_of(*second, value) && value == 2U, "its second member is 2");
        }
    }

    void refusals_are_named() {
        script::WireDoc doc{};
        const char *refusal = nullptr;
        expect(!doc.parse(bytes_of("{"), &refusal) && std::strcmp(refusal, "json-unexpected-end") == 0,
               "an unterminated object is refused by name");
        expect(!doc.parse(bytes_of("\"{\"a\": 1} trailing\""), &refusal) &&
                       std::strcmp(refusal, "json-trailing") == 0,
               "trailing bytes are refused by name");
        expect(!doc.parse(bytes_of("{\"a\": "), &refusal) && std::strcmp(refusal, "json-unexpected-end") == 0,
               "a truncated value is refused by name");
        char out[8] = {};
        expect(script::WireDoc::base64_decode("cmV0dXJuIDE=", std::span<char>(out)) == 8U &&
                       std::strncmp(out, "return 1", 8U) == 0,
               "base64 decodes a chunk");
        expect(script::WireDoc::base64_decode("not base64!", std::span<char>(out)) == 0U,
               "a malformed base64 string decodes to nothing");
    }
}  // namespace

int main() {
    std::printf("wire doc: a small tree\n");
    a_small_tree_is_reachable();
    std::printf("wire doc: named refusals\n");
    refusals_are_named();
    if (g_failures != 0U) {
        std::printf("wire doc: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("wire doc: PASS (tree reachable, refusals named)\n");
    return 0;
}
