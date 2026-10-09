#pragma once
/** @file profile/glkv3_document.hpp
 *  Reading a GLKv3 document: the one version number, and the payload family.
 *
 *  The wire is a MessagePack map whose root carries schema, and this build
 *  reads exactly one value for it; anything else is refused before a single key
 *  is looked up, because a document written for another schema would be read
 *  with the wrong expectations. Reading is in place over the caller's bytes:
 *  nothing is copied and nothing is allocated past the parse itself.
 *
 *  The payload family is looked up by the rule profile/payload_keys.hpp owns:
 *  the name must be admissible, and an entry that exists must carry the three
 *  keys that bind it to bytes. A missing entry and an entry that does not carry
 *  them are both refusals, named as the payload path already names them.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "mpack.h"

namespace ghostlock::profile {
    /** The one schema value this build reads (the tree keeps a single version). */
    inline constexpr std::uint64_t kDocumentSchema = 3U;

    /** What a document read produced. Names follow the single rejection table. */
    enum class DocumentVerdict : unsigned char {
        Ok = 0,
        /** Not a document this build reads: wrong schema, or unparseable. */
        Invalid = 1,
        /** The document is fine; what was asked for is not in it. */
        Missing = 2,
    };

    [[nodiscard]] constexpr const char *document_verdict_name(DocumentVerdict verdict) noexcept {
        switch (verdict) {
            case DocumentVerdict::Ok: return "ok";
            case DocumentVerdict::Invalid: return "document-invalid";
            case DocumentVerdict::Missing: return "payload-missing";
        }
        return "unknown";
    }

    /** One payload entry, as the document declares it. The bytes stay a view --
     *  their length is explicit -- while the two texts are copied: the wire's
     *  strings are length-delimited and carry no terminator, so anything that
     *  treated them as C strings would read past their end. */
    struct PayloadEntry final {
        const std::uint8_t *bytes = nullptr;
        std::size_t size = 0U;
        /** The declared digest, exactly the 64 hex characters, terminated here. */
        char sha256[65] = {};
        /** The declared kind, terminated here. */
        char kind[16] = {};
    };

    class Document final {
      public:
        Document() noexcept = default;
        ~Document() noexcept;
        Document(const Document &) = delete;
        Document &operator=(const Document &) = delete;

        /** Parse in place. The buffer must outlive this object's use. */
        [[nodiscard]] DocumentVerdict parse(const std::uint8_t *bytes,
                                            std::size_t size) noexcept;

        [[nodiscard]] bool is_open() const noexcept;
        [[nodiscard]] std::uint64_t schema() const noexcept;

        /** Read backend.<backend>.payload.<name>.{bin,sha256,size,kind}: the
         *  name must obey the payload rule, and the three binding keys must be
         *  present, or the verdict says so. */
        [[nodiscard]] DocumentVerdict payload(std::string_view backend,
                                             std::string_view name,
                                             PayloadEntry &out) const noexcept;

        /** Release the parse; a further read is a missing document. */
        void reset() noexcept;

      private:
        mpack_tree_t tree_{};
        bool open_ = false;
        std::uint64_t schema_ = 0U;
    };
}  // namespace ghostlock::profile
