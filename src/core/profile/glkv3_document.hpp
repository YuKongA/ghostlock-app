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

#include "contract/step_catalog.hpp"
#include "mpack.h"
#include "profile/queue_keys.hpp"

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

        /** Read one unsigned integer key out of a dotted segment. A key that is
         *  absent, or present as something other than an unsigned integer, is the
         *  same refusal: the caller asked for a number and there is none. */
        [[nodiscard]] DocumentVerdict uint_key(std::string_view segment, std::string_view key,
                                               std::uint64_t &out) const noexcept;

        /** What a scalar key carried when it was read. The caller knows which kind
         *  it declared, so the kind is reported rather than judged here: a key that
         *  is there but of the wrong kind must be distinguishable from one that is
         *  not there at all. */
        enum class ScalarKind : unsigned char {
            Uint = 0,
            Int = 1,
            Bool = 2,
        };

        /** Read one scalar key, reporting its kind: an integer is sign-extended
         *  into the value, a boolean becomes zero or one. Strings, binary, floats
         *  and absent keys are the same refusal. */
        [[nodiscard]] DocumentVerdict value_key(std::string_view segment, std::string_view key,
                                                ScalarKind &kind,
                                                std::uint64_t &out) const noexcept;

        /** Why a step queue could not be read. Names follow the single table. */
        enum class QueueVerdict : unsigned char {
            Ok = 0,
            /** The document is not one this build reads. */
            DocumentInvalid = 1,
            /** The section, the queue, or an entry's step key is absent or shaped
             *  wrongly: without a queue there is no plan to carry out. */
            QueueMissing = 2,
            /** A step token the catalogue does not declare. */
            StepUnknown = 3,
            /** The caller's array cannot hold the queue. */
            TooManySteps = 4,
        };

        [[nodiscard]] static constexpr const char *queue_verdict_name(QueueVerdict verdict) noexcept {
            switch (verdict) {
                case QueueVerdict::Ok: return "ok";
                case QueueVerdict::DocumentInvalid: return "document-invalid";
                case QueueVerdict::QueueMissing: return "plan-missing";
                case QueueVerdict::StepUnknown: return "unknown-op";
                case QueueVerdict::TooManySteps: return "budget-exceeded";
            }
            return "unknown";
        }

        /** The outcome: how many steps are usable, and which token failed. On any
         *  refusal count is zero, so a half-read queue can never be run. */
        struct StepQueueResult final {
            QueueVerdict verdict = QueueVerdict::DocumentInvalid;
            std::size_t count = 0U;
            /** The token that failed, copied: a wire string carries no terminator,
             *  so handing the caller a pointer into the document would give it a
             *  text it cannot measure. Empty when no token failed. */
            char bad_step[32] = {};
        };

        /** Read the step queue of a backend section: an array of entries, each
         *  naming one step by the token the catalogue declares. */
        [[nodiscard]] StepQueueResult step_queue(std::string_view segment, contract::StepId *out,
                                                 std::size_t capacity) const noexcept;

        /** Read one root text key (the selection tokens live at the root, beside
         *  the schema, not inside a section). The text is copied: a wire string
         *  carries no terminator, and the caller must be able to measure it. */
        [[nodiscard]] DocumentVerdict root_text(std::string_view key, char *out,
                                                std::size_t capacity) const noexcept;

        /** Release the parse; a further read is a missing document. */
        void reset() noexcept;

      private:
        mpack_tree_t tree_{};
        bool open_ = false;
        std::uint64_t schema_ = 0U;
    };
}  // namespace ghostlock::profile
