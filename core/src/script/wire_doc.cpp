/** @file script/wire_doc.cpp
 *  The wire reader: framing, a bounded JSON tree, the typed reads, and base64.
 *  See wire_doc.hpp for the design; every failure here is a name, never an exception.
 *
 *  JSON comes from the vendored nlohmann library (user ruling 2026-10-12: the hand-written
 *  parser is gone). The library is strict by default, so trailing bytes after the top-level
 *  value are its own refusal; this file only translates library errors and the post-parse
 *  bounds into the named refusals the rest of the tree expects.
 */
#include "script/wire_doc.hpp"

#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <json.hpp>

namespace ghostlock::script {
    namespace {
        constexpr std::uint32_t kNoIndex = 0xFFFFFFFFU;

        bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

        /** One hexadecimal digit, or -1. Used by the TYPED READ (u64_of), not by the parser. */
        int hex_digit(char c) noexcept {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        /** Which named refusal a library parse error earns.
         *   - "unexpected end of input"  : the document stops early  => json-unexpected-end
         *   - "expected end of input"    : content after the value   => json-trailing
         *     (the library's strict mode is what detects it; the code stays because the
         *      wire contract names the case)
         *   - anything else              : malformed token           => json-bad-token */
        const char *refusal_of_parse_error(const nlohmann::json::parse_error &error) noexcept {
            const std::string_view what(error.what());
            if (what.find("unexpected end of input") != std::string_view::npos) {
                return "json-unexpected-end";
            }
            if (what.find("expected end of input") != std::string_view::npos) {
                return "json-trailing";
            }
            return "json-bad-token";
        }

        /** Fills the caller-owned pools from a parsed document. Bounds are checked here, so
         *  an oversized or too-deep document is a name, not a partial tree. */
        struct PoolBuilder final {
            JsonNode *nodes = nullptr;
            JsonEdge *edges = nullptr;
            std::size_t *node_count = nullptr;
            std::size_t *edge_count = nullptr;
            const char **refusal = nullptr;
            std::vector<std::string> *number_texts = nullptr;

            std::uint32_t new_node(JsonNode::Kind kind) noexcept {
                if (*node_count >= WireDoc::kMaxNodes) { *refusal = "json-too-large"; return kNoIndex; }
                const std::uint32_t index = static_cast<std::uint32_t>(*node_count);
                nodes[index] = JsonNode{};
                nodes[index].kind = kind;
                ++(*node_count);
                return index;
            }
            std::uint32_t new_edge(std::string_view key, std::uint32_t node) noexcept {
                if (*edge_count >= WireDoc::kMaxEdges) { *refusal = "json-too-large"; return kNoIndex; }
                const std::uint32_t index = static_cast<std::uint32_t>(*edge_count);
                edges[index] = JsonEdge{};
                edges[index].key = key;
                edges[index].node = node;
                ++(*edge_count);
                return index;
            }

            using Kind = JsonNode::Kind;
            using Json = nlohmann::json;

            /** Append one child edge to the parent's own list (document order). */
            bool link(std::string_view key, std::uint32_t child, std::uint32_t &head, std::uint32_t &last,
                      std::uint32_t &count) noexcept {
                const std::uint32_t index = new_edge(key, child);
                if (index == kNoIndex) return false;
                if (head == kNoIndex) { head = index; } else { edges[last].next = index; }
                last = index;
                ++count;
                return true;
            }

            /** One node, then its children. Depth, node and edge bounds are checked here. */
            bool build(const Json &value, std::size_t depth, std::uint32_t &out) noexcept {
                if (depth >= WireDoc::kMaxDepth) { *refusal = "json-too-deep"; return false; }
                const bool container = value.is_array() || value.is_object();
                out = new_node(value.is_null()      ? Kind::Null
                               : value.is_boolean() ? Kind::Bool
                               : value.is_string()  ? Kind::Str
                               : container          ? (value.is_object() ? Kind::Obj : Kind::Arr)
                                                    : Kind::Num);
                if (out == kNoIndex) return false;
                if (value.is_boolean()) {
                    nodes[out].boolean = value.get<bool>();
                    return true;
                }
                if (value.is_number()) {
                    /* Num keeps the raw text: the dump of the parsed number is the decimal
                     * spelling the document carried, and u64_of reads it. */
                    number_texts->push_back(value.dump());
                    const std::string &text = number_texts->back();
                    nodes[out].text = std::string_view(text.data(), text.size());
                    return true;
                }
                if (value.is_string()) {
                    /* Str keeps the DECODED text, pointing into the held document. */
                    const std::string &text = value.get_ref<const std::string &>();
                    nodes[out].text = std::string_view(text.data(), text.size());
                    return true;
                }
                if (!container) { *refusal = "json-bad-token"; return false; }
                std::uint32_t head = kNoIndex;
                std::uint32_t last = kNoIndex;
                std::uint32_t count = 0U;
                /* key() is only legal on OBJECT iterators: on an array it throws
                 * invalid_iterator.207, which is exactly what aborted the device run. So the
                 * container kind is decided once, before the walk, and arrays pass no key. */
                const bool is_object = value.is_object();
                for (auto entry = value.begin(); entry != value.end(); ++entry) {
                    std::string_view key{};
                    if (is_object) {
                        const std::string &text = entry.key();
                        key = std::string_view(text.data(), text.size());
                    }
                    std::uint32_t child = kNoIndex;
                    if (!build(entry.value(), depth + 1U, child)) return false;
                    if (!link(key, child, head, last, count)) return false;
                }
                nodes[out].first = head == kNoIndex ? 0U : head;
                nodes[out].count = count;
                return true;
            }
        };
    }  // namespace

    /** The parsed document, held so JsonNode::text and JsonEdge::key can point into it: the
     *  views stay valid exactly as long as this storage, i.e. as long as the WireDoc.
     *  number_texts is reserved up front so element addresses never move. */
    struct WireDoc::JsonStorage final {
        nlohmann::json document{};
        std::vector<std::string> number_texts{};
    };

    WireDoc::~WireDoc() noexcept { delete json_; }

    bool WireDoc::read_framed(std::FILE *in, std::span<std::byte> out, std::size_t &size,
                              const char **refusal) noexcept {
        size = 0U;
        std::uint8_t header[4] = {};
        if (std::fread(header, 1U, sizeof(header), in) != sizeof(header)) {
            *refusal = "wire-no-frame";
            return false;
        }
        const std::size_t length = (static_cast<std::size_t>(header[0]) << 24U) |
                                   (static_cast<std::size_t>(header[1]) << 16U) |
                                   (static_cast<std::size_t>(header[2]) << 8U) |
                                   static_cast<std::size_t>(header[3]);
        if (length == 0U) { *refusal = "wire-empty"; return false; }
        if (length > out.size()) { *refusal = "wire-too-large"; return false; }
        if (std::fread(out.data(), 1U, length, in) != length) { *refusal = "wire-truncated"; return false; }
        size = length;
        return true;
    }

    /** Parse bytes into the pool.
     *  LESSON (device SIGABRT, 2026-10-12): the library THROWS for ordinary misuse too
     *  (invalid_iterator on a non-object key(), bad_alloc, ...), and this entry point is
     *  noexcept by contract. Therefore the WHOLE body -- the library call, the pool walk and
     *  the bound checks -- sits inside one try/catch, and every escape becomes a NAMED
     *  refusal instead of std::terminate. Do not lift the guard: it is the contract.
     *  @param bytes   the framed document body
     *  @param refusal receives the named reason on failure
     *  @return true when the whole document fitted the pools. */
    bool WireDoc::parse(std::span<const std::byte> bytes, const char **refusal) noexcept {
        node_count_ = 0U;
        edge_count_ = 0U;
        root_ = 0U;
        delete json_;
        json_ = nullptr;
        try {
            if (bytes.empty()) { *refusal = "json-unexpected-end"; return false; }
            if (bytes.size() > kMaxPayloadBytes) { *refusal = "json-too-large"; return false; }
            auto *storage = new JsonStorage();
            storage->number_texts.reserve(kMaxNodes);
            const char *first = reinterpret_cast<const char *>(bytes.data());
            storage->document = nlohmann::json::parse(first, first + bytes.size());
            PoolBuilder builder{};
            builder.nodes = nodes_;
            builder.edges = edges_;
            builder.node_count = &node_count_;
            builder.edge_count = &edge_count_;
            builder.refusal = refusal;
            builder.number_texts = &storage->number_texts;
            std::uint32_t root_index = kNoIndex;
            if (builder.build(storage->document, 0U, root_index)) {
                json_ = storage;
                root_ = root_index;
                return true;
            }
            delete storage;
        } catch (const nlohmann::json::parse_error &error) {
            /* Syntax trouble: the specific named code comes from the library's message. */
            *refusal = refusal_of_parse_error(error);
        } catch (const std::bad_alloc &) {
            /* An allocation failure means the document outgrew what we will hold. */
            *refusal = "json-too-large";
        } catch (const std::exception &) {
            /* Any other library exception (e.g. invalid_iterator): the document is malformed
             * for our purposes, and the contract's code for that is json-bad-token. */
            *refusal = "json-bad-token";
        } catch (...) {
            *refusal = "json-bad-token";
        }
        /* One reset for every failure path: a refused document leaves no partial tree. */
        node_count_ = 0U;
        edge_count_ = 0U;
        return false;
    }

    const JsonNode *WireDoc::root() const noexcept { return node_count_ == 0U ? nullptr : &nodes_[root_]; }
    const JsonNode *WireDoc::child(const JsonNode &object, std::string_view key) const noexcept {
        if (object.kind != JsonNode::Kind::Obj) return nullptr;
        std::uint32_t index = object.first;
        for (std::uint32_t i = 0U; i < object.count && index < kMaxEdges; ++i) {
            const JsonEdge &edge = edges_[index];
            if (edge.key == key) return &nodes_[edge.node];
            index = edge.next;
        }
        return nullptr;
    }

    const JsonNode *WireDoc::element(const JsonNode &array, std::size_t index) const noexcept {
        if (array.kind != JsonNode::Kind::Arr || index >= array.count) return nullptr;
        std::uint32_t cursor = array.first;
        for (std::size_t i = 0U; i < index; ++i) {
            if (cursor >= kMaxEdges) return nullptr;
            cursor = edges_[cursor].next;
        }
        return cursor < kMaxEdges ? &nodes_[edges_[cursor].node] : nullptr;
    }

    const JsonEdge *WireDoc::edge_at(const JsonNode &parent, std::size_t index) const noexcept {
        if ((parent.kind != JsonNode::Kind::Obj && parent.kind != JsonNode::Kind::Arr) ||
            index >= parent.count) {
            return nullptr;
        }
        std::uint32_t cursor = parent.first;
        for (std::size_t i = 0U; i < index; ++i) {
            if (cursor >= kMaxEdges) return nullptr;
            cursor = edges_[cursor].next;
        }
        return cursor < kMaxEdges ? &edges_[cursor] : nullptr;
    }

    const JsonNode *WireDoc::find(std::string_view dotted) const noexcept {
        const JsonNode *node = root();
        if (node == nullptr) return nullptr;
        std::size_t at = 0U;
        while (at < dotted.size()) {
            const std::size_t dot = dotted.find('.', at);
            const std::string_view key = dotted.substr(at, dot == std::string_view::npos ? dot : dot - at);
            if (node->kind == JsonNode::Kind::Arr) {
                std::uint64_t index = 0U;
                if (!u64_of(*node, index)) return nullptr; /* not used: arrays are walked by element() */
                node = element(*node, static_cast<std::size_t>(index));
            } else {
                node = child(*node, key);
            }
            if (node == nullptr) return nullptr;
            if (dot == std::string_view::npos) break;
            at = dot + 1U;
        }
        return node;
    }

    bool WireDoc::u64_of(const JsonNode &node, std::uint64_t &out) const noexcept {
        if (node.kind == JsonNode::Kind::Num) {
            std::uint64_t value = 0U;
            for (const char c : node.text) {
                if (!is_digit(c)) return false;
                value = value * 10U + static_cast<std::uint64_t>(c - '0');
            }
            out = value;
            return true;
        }
        if (node.kind == JsonNode::Kind::Str) {
            std::string_view text = node.text;
            unsigned base = 10U;
            if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
                base = 16U;
                text.remove_prefix(2U);
            }
            if (text.empty()) return false;
            std::uint64_t value = 0U;
            for (const char c : text) {
                const int digit = base == 16U ? hex_digit(c) : (is_digit(c) ? c - '0' : -1);
                if (digit < 0) return false;
                value = value * base + static_cast<std::uint64_t>(digit);
            }
            out = value;
            return true;
        }
        return false;
    }

    bool WireDoc::bool_of(const JsonNode &node, bool &out) const noexcept {
        if (node.kind == JsonNode::Kind::Bool) { out = node.boolean; return true; }
        std::uint64_t value = 0U;
        if (u64_of(node, value)) { out = value != 0U; return true; }
        return false;
    }

    bool WireDoc::str_of(const JsonNode &node, std::string_view &out) const noexcept {
        if (node.kind != JsonNode::Kind::Str) return false;
        out = node.text;
        return true;
    }

    std::size_t WireDoc::base64_decode(std::string_view text, std::span<char> out) noexcept {
        std::size_t written = 0U;
        std::uint32_t accumulator = 0U;
        unsigned bits = 0U;
        for (const char c : text) {
            if (c == '\n' || c == '\r' || c == '=') continue;
            /* The base64 alphabet: A-Z 0-25, a-z 26-51, 0-9 52-61, + 62, / 63. */
            int value = -1;
            if (c >= 'A' && c <= 'Z') { value = c - 'A'; }
            else if (c >= 'a' && c <= 'z') { value = (c - 'a') + 26; }
            else if (c >= '0' && c <= '9') { value = (c - '0') + 52; }
            else if (c == '+') { value = 62; }
            else if (c == '/') { value = 63; }
            if (value < 0) return 0U;
            accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
            bits += 6U;
            if (bits >= 8U) {
                bits -= 8U;
                if (written >= out.size()) return 0U;
                out[written++] = static_cast<char>((accumulator >> bits) & 0xFFU);
            }
        }
        return written;
    }
}  // namespace ghostlock::script
