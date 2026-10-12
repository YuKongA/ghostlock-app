/** @file script/wire_doc.cpp
 *  The wire reader: framing, a bounded JSON parser, the typed reads, and base64.
 *  See wire_doc.hpp for the design; every failure here is a name, never an exception.
 */
#include "script/wire_doc.hpp"

#include <cstring>

namespace ghostlock::script {
    namespace {
        constexpr std::uint32_t kNoIndex = 0xFFFFFFFFU;

        bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

        int hex_digit(char c) noexcept {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        /** The parser: a cursor over the bytes with the document's pools behind it. */
        struct Cursor final {
            std::string_view in{};
            std::size_t at = 0U;
            std::size_t depth = 0U;
            JsonNode *nodes = nullptr;
            JsonEdge *edges = nullptr;
            std::size_t *node_count = nullptr;
            std::size_t *edge_count = nullptr;
            const char **refusal = nullptr;

            /** One open container: its node index and the head/tail/count of its own edge list. */
            struct Open final {
                std::uint32_t node = kNoIndex;
                std::uint32_t head = kNoIndex;
                std::uint32_t last = kNoIndex;
                std::uint32_t count = 0U;
            };
            Open opens[WireDoc::kMaxDepth + 1U]{};
            std::size_t open_count = 0U;

            bool fail(const char *why) const noexcept {
                *refusal = why;
                return false;
            }
            bool at_end() const noexcept { return at >= in.size(); }
            void skip_ws() noexcept {
                while (!at_end()) {
                    const char c = in[at];
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++at; continue; }
                    break;
                }
            }

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

            bool literal(std::string_view word) noexcept {
                if (in.substr(at, word.size()) != word) return fail("json-bad-token");
                at += word.size();
                return true;
            }

            bool string(std::string_view &out) noexcept {
                if (at_end() || in[at] != '"') return fail("json-bad-token");
                const std::size_t start = ++at;
                while (!at_end()) {
                    const char c = in[at];
                    if (c == '\\') { at += 2U; continue; }
                    if (c == '"') { out = in.substr(start, at - start); ++at; return true; }
                    ++at;
                }
                return fail("json-unterminated");
            }

            bool number(std::string_view &out) noexcept {
                const std::size_t start = at;
                if (!at_end() && in[at] == '-') ++at;
                while (!at_end() && (is_digit(in[at]) || in[at] == '.' || in[at] == 'e' || in[at] == 'E' ||
                                     in[at] == '+' || in[at] == '-')) { ++at; }
                if (at == start) return fail("json-bad-number");
                out = in.substr(start, at - start);
                return true;
            }

            std::uint32_t value() noexcept {
                if (depth >= WireDoc::kMaxDepth) { *refusal = "json-too-deep"; return kNoIndex; }
                ++depth;
                skip_ws();
                if (at_end()) { *refusal = "json-unexpected-end"; return kNoIndex; }
                std::uint32_t index = kNoIndex;
                const char c = in[at];
                if (c == '{') {
                    index = object();
                } else if (c == '[') {
                    index = array();
                } else if (c == '"') {
                    std::string_view text{};
                    index = string(text) ? new_node(JsonNode::Kind::Str) : kNoIndex;
                    if (index != kNoIndex) nodes[index].text = text;
                } else if (c == 't') {
                    if (!literal("true")) { --depth; return kNoIndex; }
                    index = new_node(JsonNode::Kind::Bool);
                    if (index != kNoIndex) nodes[index].boolean = true;
                } else if (c == 'f') {
                    if (!literal("false")) { --depth; return kNoIndex; }
                    index = new_node(JsonNode::Kind::Bool);
                } else if (c == 'n') {
                    if (!literal("null")) { --depth; return kNoIndex; }
                    index = new_node(JsonNode::Kind::Null);
                } else {
                    std::string_view text{};
                    index = number(text) ? new_node(JsonNode::Kind::Num) : kNoIndex;
                    if (index != kNoIndex) nodes[index].text = text;
                }
                --depth;
                return index;
            }

            bool push_open(std::uint32_t node) noexcept {
                if (open_count >= WireDoc::kMaxDepth + 1U) { *refusal = "json-too-deep"; return false; }
                opens[open_count] = Open{};
                opens[open_count].node = node;
                ++open_count;
                return true;
            }
            bool link(std::string_view key, std::uint32_t child) noexcept {
                if (open_count == 0U) { *refusal = "json-bad-token"; return false; }
                const std::uint32_t index = new_edge(key, child);
                if (index == kNoIndex) return false;
                Open &parent = opens[open_count - 1U];
                if (parent.head == kNoIndex) { parent.head = index; } else { edges[parent.last].next = index; }
                parent.last = index;
                ++parent.count;
                return true;
            }
            void pop_open() noexcept {
                if (open_count == 0U) return;
                const Open &self = opens[--open_count];
                nodes[self.node].first = self.head == kNoIndex ? 0U : self.head;
                nodes[self.node].count = self.count;
            }

            std::uint32_t array() noexcept {
                ++at; /* [ */
                const std::uint32_t self = new_node(JsonNode::Kind::Arr);
                if (self == kNoIndex) return kNoIndex;
                if (!push_open(self)) return kNoIndex;
                skip_ws();
                if (!at_end() && in[at] == ']') { ++at; pop_open(); return self; }
                while (true) {
                    const std::uint32_t child = value();
                    if (child == kNoIndex) return kNoIndex;
                    if (child == kNoIndex || !link({}, child)) return kNoIndex;
                    skip_ws();
                    if (at_end()) { *refusal = "json-unexpected-end"; return kNoIndex; }
                    const char c = in[at++];
                    if (c == ']') break;
                    if (c != ',') { *refusal = "json-bad-token"; return kNoIndex; }
                }
                pop_open();
                return self;
            }

            std::uint32_t object() noexcept {
                ++at; /* { */
                const std::uint32_t self = new_node(JsonNode::Kind::Obj);
                if (self == kNoIndex) return kNoIndex;
                if (!push_open(self)) return kNoIndex;
                skip_ws();
                if (!at_end() && in[at] == '}') { ++at; pop_open(); return self; }
                while (true) {
                    skip_ws();
                    if (at_end()) { *refusal = "json-unexpected-end"; return kNoIndex; }
                    std::string_view key{};
                    if (!string(key)) return kNoIndex;
                    skip_ws();
                    if (at_end() || in[at] != ':') { *refusal = "json-bad-token"; return kNoIndex; }
                    ++at;
                    const std::uint32_t child = value();
                    if (child == kNoIndex) return kNoIndex;
                    if (child == kNoIndex || !link(key, child)) return kNoIndex;
                    skip_ws();
                    if (at_end()) { *refusal = "json-unexpected-end"; return kNoIndex; }
                    const char c = in[at++];
                    if (c == '}') break;
                    if (c != ',') { *refusal = "json-bad-token"; return kNoIndex; }
                }
                pop_open();
                return self;
            }
        };
    }  // namespace

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

    bool WireDoc::parse(std::span<const std::byte> bytes, const char **refusal) noexcept {
        node_count_ = 0U;
        edge_count_ = 0U;
        root_ = 0U;
        const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        Cursor cursor{};
        cursor.in = text;
        cursor.nodes = nodes_;
        cursor.edges = edges_;
        cursor.node_count = &node_count_;
        cursor.edge_count = &edge_count_;
        cursor.refusal = refusal;
        const std::uint32_t root = cursor.value();
        if (root == kNoIndex) return false;
        cursor.skip_ws();
        if (!cursor.at_end()) { *refusal = "json-trailing"; return false; }
        root_ = root;
        return true;
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
