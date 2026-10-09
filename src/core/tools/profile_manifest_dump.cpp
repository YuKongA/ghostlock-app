/** @file tools/profile_manifest_dump.cpp
 *  Write the profile manifest to stdout, out of the table that owns it.
 *
 *  Host tool: the manifest crosses to the app, so it is generated here and the two copies
 *  the app reads are written from the same bytes. A tool that failed to write says so and
 *  exits non-zero rather than leaving half a file behind.
 */
#include <cstdio>

#include "profile/key_catalog.hpp"

int main() {
    if (!ghostlock::profile::write_manifest(stdout)) {
        std::fprintf(stderr, "profile-manifest-v3: could not write the manifest\n");
        return 1;
    }
    return 0;
}
