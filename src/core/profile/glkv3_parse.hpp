#ifndef GHOSTLOCK_PROFILE_GLKV3_PARSE_HPP
#define GHOSTLOCK_PROFILE_GLKV3_PARSE_HPP

/* GLKv3 (MessagePack) decode bridge (GLKv3-4).
 *
 * GLKv3 has no container header: the root is a MessagePack map carrying a
 * mandatory "schema": 3 and the token selection (release / terminal / backend /
 * route). This unit decodes it with the mature MPack codec
 * (profile/glkv3.cpp) against the combined root + 43499 + 43284 FieldSpec
 * universe, maps the selection tokens to the binary ids and the typed section
 * values to the neutral profile::Document, then reuses binary_profile::
 * bind_document so a v3 document lands exactly the values a v2 document with
 * the same logical content does.
 *
 * The decoder is fail-closed: Production rejects a non-map root, a missing or
 * non-3 schema, an unknown section/key, a type mismatch and an unknown or
 * unresolved selection. Looks-like-v3 is decided by the root map marker alone;
 * a v3-shaped document that fails to decode is rejected whole, never retried
 * as v2. */

#include "profile/binary.h"
#include "profile/model.h"

#include <cstddef>
#include <string_view>

namespace ghostlock::binary_profile {
    /* True when the first wire byte is a MessagePack map marker (fixmap/map16/
     * map32). The v2 transport starts with the little-endian magic 0x21, so the
     * two formats never alias. */
    [[nodiscard]] bool looks_like_glkv3(std::string_view document) noexcept;

    int32_t parse_v3(std::string_view document, struct ghostlock::profile::kernel_offsets *out,
                     char *release_buf, size_t release_buf_cap,
                     component_ids *ids = nullptr,
                     struct ghostlock::profile::Document *document_out = nullptr,
                     struct ghostlock::backend::Cve2026_43284Profile *profile_43284_out = nullptr);
} // namespace ghostlock::binary_profile

#endif
