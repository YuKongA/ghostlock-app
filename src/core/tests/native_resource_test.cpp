#include "support/native_resource.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>

namespace {

void *increment(void *argument) {
  static_cast<std::atomic<int> *>(argument)->fetch_add(1);
  return nullptr;
}

}  // namespace

int main() {
  int pipe_fd[2];
  assert(pipe(pipe_fd) == 0);
  close(pipe_fd[1]);
  const int observed = pipe_fd[0];
  {
    ghostlock::UniqueFd first(observed);
    ghostlock::UniqueFd second(std::move(first));
    assert(!first.valid() && second.get() == observed);
  }
  errno = 0;
  assert(fcntl(observed, F_GETFD) == -1 && errno == EBADF);

  auto mapping = ghostlock::MappedRegion::map_anonymous(
      4096, PROT_READ | PROT_WRITE);
  assert(mapping && mapping.value().bytes().size() == 4096);
  mapping.value().bytes()[0] = std::byte{0x5a};

  std::atomic<int> calls{0};
  ghostlock::PthreadOwner worker;
  assert(worker.start(increment, &calls) == 0);
  assert(worker.join() == 0 && calls.load() == 1);

  const pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    for (;;) pause();
  }
  ghostlock::ChildProcess child(pid);
  assert(child.terminate_and_wait(SIGKILL) == 0);
  assert(!child.valid());
  return 0;
}
