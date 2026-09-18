/* Host fixed-vector test for the KernelSU handoff probes. Locks the legacy
 * marker matching and the poll-policy short circuits; the enforce path is a
 * real syscall and is only checked when the host exposes the file. */

#include "session/handoff_probe.hpp"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

namespace {

struct TempLogDir {
  char path[64];
  std::string log_path;
};

TempLogDir make_temp_log_dir(const char *contents) {
  TempLogDir dir{};
  snprintf(dir.path, sizeof(dir.path), "/tmp/ghostlock_handoff_XXXXXX");
  assert(mkdtemp(dir.path) != nullptr);
  dir.log_path = std::string(dir.path) + "/.ghostlock_ksu.log";
  if (contents) {
    FILE *file = fopen(dir.log_path.c_str(), "w");
    assert(file != nullptr);
    fputs(contents, file);
    fclose(file);
  }
  return dir;
}

void remove_temp_log_dir(const TempLogDir &dir) {
  unlink(dir.log_path.c_str());
  rmdir(dir.path);
}

}  // namespace

int main(void) {
  using ghostlock::HandoffPollPolicy;
  using ghostlock::HandoffProbeResult;

  bool loaded = false;
  bool failed = false;
  assert(!ghostlock::scan_ksu_log("/nonexistent/.ghostlock_ksu.log",
                                  loaded, failed));
  assert(!loaded && !failed);

  TempLogDir loaded_dir = make_temp_log_dir(
          "[*] unrelated\n[+] KernelSU module loaded\n");
  loaded = false;
  failed = false;
  assert(ghostlock::scan_ksu_log(loaded_dir.log_path, loaded, failed));
  assert(loaded && !failed);

  TempLogDir already_dir = make_temp_log_dir("[+] KernelSU already loaded\n");
  loaded = false;
  failed = false;
  assert(ghostlock::scan_ksu_log(already_dir.log_path, loaded, failed));
  assert(loaded && !failed);

  TempLogDir failed_dir = make_temp_log_dir("[!] KernelSU module not loaded\n");
  loaded = false;
  failed = false;
  assert(ghostlock::scan_ksu_log(failed_dir.log_path, loaded, failed));
  assert(!loaded && failed);

  /* Both markers accumulate independently, mirroring the legacy scan. */
  TempLogDir both_dir = make_temp_log_dir(
          "[!] KernelSU module not loaded\n[+] KernelSU already loaded\n");
  loaded = false;
  failed = false;
  assert(ghostlock::scan_ksu_log(both_dir.log_path, loaded, failed));
  assert(loaded && failed);

  /* A zero policy performs no poll and no sleep. */
  HandoffPollPolicy idle_policy;
  idle_policy.module_poll_attempts = 0;
  idle_policy.enforce_poll_attempts = 0;
  idle_policy.log_poll_attempts = 0;
  const HandoffProbeResult idle =
          ghostlock::handoff_probe_run(idle_policy, loaded_dir.path);
  assert(!idle.module_visible && !idle.ksu_log_loaded &&
         !idle.ksu_log_failed && !idle.enforce_ok && !idle.ready());

  /* The log marker wins without any module visibility, and the default
   * enforce cadence probes once. */
  HandoffPollPolicy policy;
  policy.module_poll_attempts = 0;
  policy.enforce_poll_attempts = 1;
  policy.enforce_poll_interval_ms = 0;
  policy.log_poll_attempts = 1;
  policy.log_poll_interval_ms = 0;
  const HandoffProbeResult probe =
          ghostlock::handoff_probe_run(policy, loaded_dir.path);
  assert(!probe.module_visible);
  assert(probe.ksu_log_loaded && !probe.ksu_log_failed);
  assert(probe.ready());
  if (access("/sys/fs/selinux/enforce", F_OK) != 0) {
    /* Denied read means enforcing. */
    assert(probe.enforce_ok);
  }

  /* A missing home directory leaves the log probes false. */
  const HandoffProbeResult missing = ghostlock::handoff_probe_run(
          policy, "/nonexistent-home");
  assert(!missing.ksu_log_loaded && !missing.ksu_log_failed);
  assert(!missing.ready());

  remove_temp_log_dir(loaded_dir);
  remove_temp_log_dir(already_dir);
  remove_temp_log_dir(failed_dir);
  remove_temp_log_dir(both_dir);

  puts("handoff_probe_test: ok");
  return 0;
}
