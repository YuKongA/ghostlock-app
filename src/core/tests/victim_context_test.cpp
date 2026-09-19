/* Host lifecycle test for VictimContext. Locks the explicit retirement
 * semantics: scope exit never signals a child, retire() kills and reaps only
 * the pid the context still owns, and release()/mark_child_exited() drop the
 * pid without touching the process. */

#include "session/victim_context.hpp"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>

using namespace ghostlock;

namespace {

pid_t spawn_sleeper() {
  const pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    for (;;) pause();
  }
  return child;
}

void reap_sleeper(pid_t child) {
  kill(child, SIGKILL);
  waitpid(child, nullptr, 0);
}

}  // namespace

int main(void) {

  VictimContext context;
  assert(context.child() == -1);
  assert(!context.task_read.valid());

  /* retire() kills and reaps a child the context still owns. */
  const pid_t owned = spawn_sleeper();
  context.set_child(owned);
  assert(context.child() == owned);
  context.retire_child();
  assert(context.child() == -1);
  errno = 0;
  assert(waitpid(owned, nullptr, WNOHANG) == -1 && errno == ECHILD);
  context.retire_child();

  /* release() transfers the pid away; retire() must not signal it. */
  const pid_t transferred = spawn_sleeper();
  context.set_child(transferred);
  assert(context.release_child() == transferred);
  assert(context.child() == -1);
  context.retire_child();
  assert(kill(transferred, 0) == 0);
  reap_sleeper(transferred);

  /* mark_child_exited() drops a reaped pid without signalling. */
  const pid_t already_reaped = spawn_sleeper();
  context.set_child(already_reaped);
  context.mark_child_exited();
  assert(context.child() == -1);
  context.retire_child();
  assert(kill(already_reaped, 0) == 0);
  reap_sleeper(already_reaped);

  puts("victim_context_test: ok");
  return 0;
}
