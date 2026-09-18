/* Host fixed-vector test for runtime path normalization. Locks the legacy
 * snprintf truncation and trailing-slash rules used by RuntimeConfig. */

#include "../session/runtime_paths.h"

#include <assert.h>
#include <stdio.h>

#include <string>

int main(void) {
  using ghostlock::runtime_paths::kHomeDirCapacity;
  using ghostlock::runtime_paths::normalize_home_dir;
  using ghostlock::runtime_paths::root_script_file;

  assert(normalize_home_dir("/data/local/tmp") == "/data/local/tmp");
  assert(normalize_home_dir("/data/local/tmp/") == "/data/local/tmp");
  assert(normalize_home_dir("/data//tmp///") == "/data//tmp");
  assert(normalize_home_dir("/") == "/");
  assert(normalize_home_dir("") == "");
  assert(normalize_home_dir("///") == "/");

  assert(root_script_file("/data/local/tmp") ==
         "/data/local/tmp/.ghostlock_root.sh");
  assert(root_script_file("") == "/.ghostlock_root.sh");

  const std::string long_home(kHomeDirCapacity + 200, 'a');
  const std::string normalized = normalize_home_dir(long_home);
  assert(normalized.size() == kHomeDirCapacity - 1);
  const std::string script = root_script_file(normalized);
  assert(script.size() == normalized.size() + 19);
  assert(script.size() <=
         ghostlock::runtime_paths::kRootScriptPathCapacity - 1);

  puts("runtime_paths_test: ok");
  return 0;
}
