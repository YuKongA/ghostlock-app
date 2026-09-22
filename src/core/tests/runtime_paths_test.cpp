/* Host fixed-vector test for runtime path normalization. Locks the legacy
 * snprintf truncation and trailing-slash rules used by RuntimeConfig. */

#include "../session/runtime_paths.h"

#include <cassert>
#include <cstdio>

#include <string>

using namespace ghostlock;

int main(void) {

  assert(runtime_paths::normalize_home_dir("/data/local/tmp") == "/data/local/tmp");
  assert(runtime_paths::normalize_home_dir("/data/local/tmp/") == "/data/local/tmp");
  assert(runtime_paths::normalize_home_dir("/data//tmp///") == "/data//tmp");
  assert(runtime_paths::normalize_home_dir("/") == "/");
  assert(runtime_paths::normalize_home_dir("") == "");
  assert(runtime_paths::normalize_home_dir("///") == "/");

  assert(runtime_paths::root_script_file("/data/local/tmp") ==
         "/data/local/tmp/.ghostlock_root.sh");
  assert(runtime_paths::root_script_file("") == "/.ghostlock_root.sh");

  const std::string long_home(runtime_paths::kHomeDirCapacity + 200, 'a');
  const std::string normalized = runtime_paths::normalize_home_dir(long_home);
  assert(normalized.size() == runtime_paths::kHomeDirCapacity - 1);
  const std::string script = runtime_paths::root_script_file(normalized);
  assert(script.size() == normalized.size() + 19);
  assert(script.size() <=
         runtime_paths::kRootScriptPathCapacity - 1);

  puts("runtime_paths_test: ok");
  return 0;
}
