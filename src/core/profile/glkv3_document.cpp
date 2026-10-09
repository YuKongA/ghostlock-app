/** @file profile/glkv3_document.cpp
 *  Parsing the wire and reading the payload family out of it.
 */
#include "profile/glkv3_document.hpp"

#include "profile/payload_keys.hpp"
#include "support/sha256.hpp"

namespace ghostlock::profile {
    namespace {
        /** Copy a caller key into a bounded buffer; false when it does not fit. */
        bool copy_bounded(char *out, std::size_t capacity, std::string_view text) noexcept {
            if (text.size() + 1U > capacity) {
                return false;
            }
            for (std::size_t i = 0U; i < text.size(); ++i) {
                out[i] = text[i];
            }
            out[text.size()] = '\0';
            return true;
        }

        bool is_map(mpack_node_t node) noexcept {
            return mpack_node_type(node) == mpack_type_map;
        }

        bool text_of(mpack_node_t node, const char **out, std::size_t *length) noexcept {
            if (mpack_node_type(node) != mpack_type_str) {
                return false;
            }
            *out = mpack_node_str(node);
            *length = mpack_node_strlen(node);
            return *out != nullptr;
        }
    }  // namespace

    Document::~Document() noexcept { reset(); }

    bool Document::is_open() const noexcept { return open_; }

    std::uint64_t Document::schema() const noexcept { return schema_; }

    void Document::reset() noexcept {
        if (open_) {
            mpack_tree_destroy(&tree_);
            open_ = false;
        }
        schema_ = 0U;
    }

    DocumentVerdict Document::parse(const std::uint8_t *bytes, std::size_t size) noexcept {
        reset();
        if (bytes == nullptr || size == 0U) {
            return DocumentVerdict::Invalid;
        }
        mpack_tree_init_data(&tree_, reinterpret_cast<const char *>(bytes), size);
        mpack_tree_parse(&tree_);
        if (mpack_tree_error(&tree_) != mpack_ok) {
            mpack_tree_destroy(&tree_);
            return DocumentVerdict::Invalid;
        }
        open_ = true;
        const mpack_node_t schema_node =
            mpack_node_map_cstr_optional(mpack_tree_root(&tree_), "schema");
        if (mpack_node_type(schema_node) != mpack_type_uint) {
            reset();
            return DocumentVerdict::Invalid;
        }
        schema_ = mpack_node_u64(schema_node);
        if (schema_ != kDocumentSchema) {
            reset();
            return DocumentVerdict::Invalid;
        }
        return DocumentVerdict::Ok;
    }

    DocumentVerdict Document::uint_key(std::string_view segment, std::string_view key,
                                       std::uint64_t &out) const noexcept {
        out = 0U;
        if (!open_ || segment.empty() || key.empty()) {
            return DocumentVerdict::Invalid;
        }
        char segment_text[192];
        char key_text[96];
        if (!copy_bounded(segment_text, sizeof(segment_text), segment) ||
            !copy_bounded(key_text, sizeof(key_text), key)) {
            return DocumentVerdict::Missing;
        }
        /* The whole chain is resolved from the root for every key. A node kept
         * from an earlier lookup carries parse state, and reading a wide value
         * after one makes the next lookup answer with nothing: resolving again
         * costs a walk and is the difference between reading a document and
         * reading part of one. The value is read as a 64-bit integer -- an
         * unsigned int accessor truncates anything wider than 32 bits, which is
         * how a two-gigabyte-and-a-bit offset came back as zero. */
        mpack_tree_t *tree = const_cast<mpack_tree_t *>(&tree_);
        const mpack_node_t sections =
            mpack_node_map_cstr_optional(mpack_tree_root(tree), "sections");
        if (!is_map(sections)) {
            return DocumentVerdict::Missing;
        }
        const mpack_node_t node = mpack_node_map_cstr_optional(sections, segment_text);
        if (!is_map(node)) {
            return DocumentVerdict::Missing;
        }
        const mpack_node_t value = mpack_node_map_cstr_optional(node, key_text);
        if (mpack_node_type(value) != mpack_type_uint) {
            return DocumentVerdict::Missing;
        }
        out = mpack_node_u64(value);
        return DocumentVerdict::Ok;
    }

    DocumentVerdict Document::value_key(std::string_view segment, std::string_view key,
                                        ScalarKind &kind, std::uint64_t &out) const noexcept {
        kind = ScalarKind::Uint;
        out = 0U;
        if (!open_ || segment.empty() || key.empty()) {
            return DocumentVerdict::Invalid;
        }
        char segment_text[192];
        char key_text[96];
        if (!copy_bounded(segment_text, sizeof(segment_text), segment) ||
            !copy_bounded(key_text, sizeof(key_text), key)) {
            return DocumentVerdict::Missing;
        }
        /* Resolved from the root on every call: see uint_key. */
        mpack_tree_t *tree = const_cast<mpack_tree_t *>(&tree_);
        const mpack_node_t sections =
            mpack_node_map_cstr_optional(mpack_tree_root(tree), "sections");
        if (!is_map(sections)) {
            return DocumentVerdict::Missing;
        }
        const mpack_node_t node = mpack_node_map_cstr_optional(sections, segment_text);
        if (!is_map(node)) {
            return DocumentVerdict::Missing;
        }
        const mpack_node_t value = mpack_node_map_cstr_optional(node, key_text);
        const mpack_type_t type = mpack_node_type(value);
        if (type == mpack_type_uint) {
            kind = ScalarKind::Uint;
            out = mpack_node_u64(value);
            return DocumentVerdict::Ok;
        }
        if (type == mpack_type_int) {
            kind = ScalarKind::Int;
            out = static_cast<std::uint64_t>(mpack_node_i64(value));
            return DocumentVerdict::Ok;
        }
        if (type == mpack_type_bool) {
            kind = ScalarKind::Bool;
            out = mpack_node_bool(value) ? 1U : 0U;
            return DocumentVerdict::Ok;
        }
        return DocumentVerdict::Missing;
    }

    DocumentVerdict Document::payload(std::string_view backend, std::string_view name,
                                      PayloadEntry &out) const noexcept {
        out = PayloadEntry{};
        if (!open_) {
            return DocumentVerdict::Invalid;
        }
        /* The name rule comes first: an inadmissible name is not looked up at
         * all, so the document is never asked about a path the tree owns. */
        if (!payload_name_is_valid(name)) {
            return DocumentVerdict::Missing;
        }
        char backend_key[64];
        char name_key[64];
        if (!copy_bounded(backend_key, sizeof(backend_key), backend) ||
            !copy_bounded(name_key, sizeof(name_key), name)) {
            return DocumentVerdict::Missing;
        }
        /* The wire keeps its segments in one flat map at the root, keyed by the
         * dotted name; the backend token in the root is a token, not a container.
         * The segment name comes from the one spelling the builder owns, so a
         * reader and a writer cannot disagree about where a payload lives. */
        mpack_tree_t *tree = const_cast<mpack_tree_t *>(&tree_);
        const mpack_node_t sections =
            mpack_node_map_cstr_optional(mpack_tree_root(tree), "sections");
        if (!is_map(sections)) {
            return DocumentVerdict::Missing;
        }
        char segment[192];
        if (!payload_segment(segment, sizeof(segment), backend_key, name_key)) {
            return DocumentVerdict::Missing;
        }
        if (!is_map(mpack_node_map_cstr_optional(sections, segment))) {
            return DocumentVerdict::Missing;
        }
        /* An entry that exists must carry what binds it to bytes: the three
         * keys are required together, and the kind must be one of the declared
         * values. Anything else is the same refusal as an absent entry. */
        /* Each key is looked up from a freshly resolved segment: a node kept from
         * an earlier lookup carries parse state, and a value read after another
         * lookup can come back empty for the same reason uint_key resolves its
         * chain per key. */
        char segment_text[192];
        char name_text[96];
        if (!copy_bounded(segment_text, sizeof(segment_text), backend) ||
            !copy_bounded(name_text, sizeof(name_text), name)) {
            return DocumentVerdict::Missing;
        }
        const auto segment_node = [&]() noexcept {
            const mpack_node_t sections_now =
                mpack_node_map_cstr_optional(mpack_tree_root(tree), "sections");
            return mpack_node_map_cstr_optional(sections_now, segment);
        };
        const mpack_node_t bin = mpack_node_map_cstr_optional(segment_node(), kPayloadKeys[0]);
        const mpack_node_t digest = mpack_node_map_cstr_optional(segment_node(), kPayloadKeys[1]);
        const mpack_node_t size_node = mpack_node_map_cstr_optional(segment_node(), kPayloadKeys[2]);
        const mpack_node_t kind = mpack_node_map_cstr_optional(segment_node(), kPayloadKeys[3]);
        if (mpack_node_type(bin) != mpack_type_bin || mpack_node_type(digest) != mpack_type_str ||
            mpack_node_type(size_node) != mpack_type_uint) {
            return DocumentVerdict::Missing;
        }
        /* A wire string carries its length and no terminator, so it is copied by
         * that length into a bounded buffer; a digest that is not exactly the 64
         * characters, or a kind longer than the declared values, is the same
         * refusal as an absent entry. */
        const char *digest_text = nullptr;
        std::size_t digest_length = 0U;
        if (!text_of(digest, &digest_text, &digest_length) ||
            digest_length != support::kSha256HexBytes - 1U ||
            digest_length + 1U > sizeof(out.sha256)) {
            return DocumentVerdict::Missing;
        }
        for (std::size_t i = 0U; i < digest_length; ++i) {
            out.sha256[i] = digest_text[i];
        }
        out.sha256[digest_length] = '\0';
        if (!support::sha256_text_is_well_formed(out.sha256)) {
            return DocumentVerdict::Missing;
        }
        const char *kind_text = nullptr;
        std::size_t kind_length = 0U;
        if (!text_of(kind, &kind_text, &kind_length) ||
            kind_length + 1U > sizeof(out.kind)) {
            return DocumentVerdict::Missing;
        }
        for (std::size_t i = 0U; i < kind_length; ++i) {
            out.kind[i] = kind_text[i];
        }
        out.kind[kind_length] = '\0';
        if (!payload_kind_is_declared(std::string_view(out.kind, kind_length))) {
            return DocumentVerdict::Missing;
        }
        const std::uint8_t *bytes =
            reinterpret_cast<const std::uint8_t *>(mpack_node_bin_data(bin));
        const std::size_t size = mpack_node_bin_size(bin);
        if (bytes == nullptr || size == 0U) {
            return DocumentVerdict::Missing;
        }
        if (mpack_node_u64(size_node) != static_cast<std::uint64_t>(size)) {
            return DocumentVerdict::Missing;
        }
        out.bytes = bytes;
        out.size = size;
        return DocumentVerdict::Ok;
    }
}  // namespace ghostlock::profile
