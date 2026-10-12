#pragma once
/** @file script/wire_doc.hpp
 *  Reading the wire: the framed input, a bounded JSON tree, and the lookups the host needs.
 *
 *  设计（与 schema 一致）：
 *   1. 输入是**长度前缀 framing**（4 字节大端长度 + 载荷），与其他 stdin 通道同一规则；
 *   2. JSON 解析**有界**：节点与字节都有上限，超限是具名拒绝（depth/size），不是崩溃；
 *   3. 数字按「十进制字符串」或裸数字读；地址/偏移按 `0x` 前缀十六进制字符串读（schema 的 hex-u64）；
 *   4. 不做裁剪、不改写：解析出来的树就是文档本身，只在调用点做具名拒绝。
 *
 * 失败语义：所有入口返回 bool + 具名 refusal（const char*），绝不抛异常、不分配无界内存。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

namespace ghostlock::script {
    /** One JSON node, stored in a caller-owned pool: indices, not pointers, so the tree is
     *  movable and its size is fixed by the pool. */
    struct JsonNode final {
        enum class Kind : unsigned char { Null = 0, Bool = 1, Num = 2, Str = 3, Arr = 4, Obj = 5 };
        Kind kind = Kind::Null;
        bool boolean = false;
        /** Num: the raw text; Str: the decoded text. Both point into the input bytes. */
        std::string_view text{};
        std::uint32_t first = 0U; /* Arr/Obj: first child */
        std::uint32_t count = 0U;
    };

    /** A child edge: key (for objects) and the child node index. */
    struct JsonEdge final {
        std::string_view key{};
        std::uint32_t node = 0U;
        /** Next sibling in this parent's own edge list (kNoIndex ends it). The list is what makes
         *  a parent's children exactly its children: a nested subtree can never leak into it. */
        std::uint32_t next = 0xFFFFFFFFU;
    };

    class WireDoc final {
      public:
        static constexpr std::size_t kMaxNodes = 8192U;
        static constexpr std::size_t kMaxEdges = 16384U;
        static constexpr std::size_t kMaxDepth = 32U;
        static constexpr std::size_t kMaxPayloadBytes = 1U << 20U;

        WireDoc() noexcept = default;

        /** Read one framed message from a stream (4-byte big-endian length, then the body).
         *  @param in       the stream
         *  @param out      receives the body bytes (caller-owned buffer)
         *  @param size     receives the body size
         *  @param refusal  receives a named reason on failure
         *  @return true when a whole body was read. */
        [[nodiscard]] static bool read_framed(std::FILE *in, std::span<std::byte> out,
                                              std::size_t &size, const char **refusal) noexcept;

        /** Parse bytes into the pool. @return true, or a named refusal. */
        [[nodiscard]] bool parse(std::span<const std::byte> bytes, const char **refusal) noexcept;

        [[nodiscard]] bool empty() const noexcept { return node_count_ == 0U; }
        [[nodiscard]] const JsonNode *root() const noexcept;
        [[nodiscard]] std::size_t node_count() const noexcept { return node_count_; }

        /** Look a node up by dotted path (a.b.c). Returns nullptr when absent. */
        [[nodiscard]] const JsonNode *find(std::string_view dotted) const noexcept;
        /** Child lookup on an object node. */
        [[nodiscard]] const JsonNode *child(const JsonNode &object,
                                            std::string_view key) const noexcept;
        /** Array element. */
        [[nodiscard]] const JsonNode *element(const JsonNode &array, std::size_t index) const noexcept;

        /** The i-th edge of an object or array (key is empty for arrays): document order. */
        [[nodiscard]] const JsonEdge *edge_at(const JsonNode &parent, std::size_t index) const noexcept;

        /** Read a u64: a bare number, or a 0x-prefixed hexadecimal string (schema hex-u64). */
        [[nodiscard]] bool u64_of(const JsonNode &node, std::uint64_t &out) const noexcept;
        /** Read a bool: a JSON bool, or 0/1 number. */
        [[nodiscard]] bool bool_of(const JsonNode &node, bool &out) const noexcept;
        /** Read a string. */
        [[nodiscard]] bool str_of(const JsonNode &node, std::string_view &out) const noexcept;

        /** Decode base64 into out. @return the decoded size, or 0 on a malformed input. */
        [[nodiscard]] static std::size_t base64_decode(std::string_view text,
                                                       std::span<char> out) noexcept;

      private:
        JsonNode nodes_[kMaxNodes]{};
        JsonEdge edges_[kMaxEdges]{};
        std::size_t node_count_ = 0U;
        std::size_t edge_count_ = 0U;
        std::uint32_t root_ = 0U;
    };
}  // namespace ghostlock::script
