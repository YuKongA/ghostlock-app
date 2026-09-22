/* Host fixed-vector test for runtime path normalization. Locks the legacy
 * snprintf truncation and trailing-slash rules used by RuntimeConfig. */

#include "../session/runtime_paths.h"

#include <cassert>
#include <cstdio>

#include <string>

using namespace ghostlock;
using namespace ghostlock::config;

int main(void) {
    assert(config::normalize_home_dir("/data/local/tmp") == "/data/local/tmp");
    assert(config::normalize_home_dir("/data/local/tmp/") == "/data/local/tmp");
    assert(config::normalize_home_dir("/data//tmp///") == "/data//tmp");
    assert(config::normalize_home_dir("/") == "/");
    assert(config::normalize_home_dir("") == "");
    assert(config::normalize_home_dir("///") == "/");

    assert(config::root_script_file("/data/local/tmp") ==
           "/data/local/tmp/.ghostlock_root.sh");
    assert(config::root_script_file("") == "/.ghostlock_root.sh");

    const std::string long_home(config::kHomeDirCapacity + 200, 'a');
    const std::string normalized = config::normalize_home_dir(long_home);
    assert(normalized.size() == config::kHomeDirCapacity - 1);
    const std::string script = config::root_script_file(normalized);
    assert(script.size() == normalized.size() + 19);
    assert(script.size() <=
           config::kRootScriptPathCapacity - 1);

    puts("runtime_paths_test: ok");
    return 0;
}
