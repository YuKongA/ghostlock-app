#include "session/victim_context.hpp"

#include <signal.h>
#include <sys/wait.h>

#include <cerrno>

namespace ghostlock {

void VictimContext::retire_child() noexcept {
  const pid_t child = release_child();
  if (child <= 0) return;
  if (kill(child, SIGKILL) != 0 && errno != ESRCH) return;
  while (waitpid(child, nullptr, 0) < 0) {
    if (errno != EINTR) return;
  }
}

}  // namespace ghostlock
