#include "legacy_support/legacy_entrypoint_starter.h"

#include "common.h"
#include "legacy_support/legacy_profile_converter.h"

#include <stdio.h>
#include <sys/utsname.h>

namespace ghostlock::legacy_support {

namespace {

const char *home_dir() {
    const char *home = getenv("GHOSTLOCK_HOME");
    if (home && home[0]) return home;
    home = getenv("TMPDIR");
    if (home && home[0]) return home;
    return "/data/local/tmp";
}

}  // namespace

int start_legacy_entrypoint(struct kernel_offsets *out, char *release_buf,
        size_t release_buf_cap) {
    struct utsname uts;
    if (uname(&uts) < 0) return -1;
    pr_info("kernel: %s\n", uts.release);

    std::array<char, 320> path{};
    snprintf(path.data(), path.size(), "%s/offsets.json", home_dir());
    if (convert_legacy_offsets(path.data(), uts.release, out, release_buf,
                release_buf_cap) != 0) {
        pr_error("no matching offsets.json entry for %s at %s\n", uts.release,
                path.data());
        return -1;
    }
    pr_success("legacy offsets loaded: %s\n", out->uname_r);
    return 0;
}

}  // namespace ghostlock::legacy_support
