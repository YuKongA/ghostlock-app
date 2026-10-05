#ifndef GHOSTLOCK_PROFILE_GLKV3_PARSE_HPP
#define GHOSTLOCK_PROFILE_GLKV3_PARSE_HPP

/* GLKv3 (MessagePack) framing bridge (GLKv3-4, A2-5).
 *
 * GLKv3 has no container header: the root is a MessagePack map carrying a
 * mandatory "schema": 3 and the token selection (release / terminal / backend /
 * route). This unit decodes it with the mature MPack codec (profile/glkv3.cpp)
 * in its neutral (schema-free) mode and converts the result to the neutral
 * profile::Document. It does not know any owner: unknown sections/keys are
 * preserved for the backend bind to reject, the route token is resolved through
 * the route catalog (a transport-level vocabulary), and the terminal/backend
 * tokens are carried verbatim in the Document for the composition root to map
 * onto the component vocabulary it owns.
 *
 * The decoder is fail-closed: Production rejects a non-map root, a missing or
 * non-3 schema, a non-string selection field and a malformed section value.
 * Looks-like-v3 is decided by the root map marker alone; a v3-shaped document
 * that fails to decode is rejected whole, never retried as v2. */

#include "profile/document.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::binary_profile {
    /* True when the first wire byte is a MessagePack map marker (fixmap/map16/
     * map32). The v2 transport starts with the little-endian magic 0x21, so the
     * two formats never alias. */
    [[nodiscard]] bool looks_like_glkv3(std::string_view document) noexcept;

    /* Frame a GLKv3 document into the neutral Document. Leaves terminal_token /
     * backend_token for the composition root to resolve; resolves the route
     * token to the wire route value (Auto when absent). */
    int32_t frame_v3(std::string_view document,
                     struct ghostlock::profile::Document *out);
} // namespace ghostlock::binary_profile

#endif
