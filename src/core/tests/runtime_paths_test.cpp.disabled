/* Host fixed-vector test for runtime path normalization. Locks the legacy
 * snprintf truncation and trailing-slash rules used by RuntimeConfig. */

#include "../session/runtime_paths.h"

#include <cassert>
#include <cstdio>

#include <string>

using namespace ghostlock;

int32_t main(void) {
    assert(config::normalize_home_dir("/data/local/tmp") == "/data/local/tmp");
    assert(config::normalize_home_dir("/data/local/tmp/") == "/data/local/tmp");
    assert(config::normalize_home_dir("/data//tmp///") == "/data//tmp");
    assert(config::normalize_home_dir("/") == "/");
    assert(config::normalize_home_dir("") == "");
    assert(config::normalize_home_dir("///") == "/");

    assert(config::root_script_file("/data/local/tmp") ==
           "/data/local/tmp/.ghostlock_root.sh");
    assert(config::root_script_file("") == "/.ghostlock_root.sh");

    /* CVE-2026-43284 convention module path: home + "/helper.ko". */
    assert(config::helper_module_file("/data/local/tmp") ==
           "/data/local/tmp/helper.ko");
    assert(config::helper_module_file("") == "/helper.ko");

    const std::string long_home(config::kHomeDirCapacity + 200, 'a');
    const std::string normalized = config::normalize_home_dir(long_home);
    assert(normalized.size() == config::kHomeDirCapacity - 1);
    const std::string script = config::root_script_file(normalized);
    assert(script.size() == normalized.size() + 19);
    assert(script.size() <=
           config::kRootScriptPathCapacity - 1);
    const std::string module = config::helper_module_file(normalized);
    assert(module.size() <= config::kHelperModulePathCapacity - 1);
    assert(module.size() >= 9U);

    puts("runtime_paths_test: ok");
    return 0;
}
