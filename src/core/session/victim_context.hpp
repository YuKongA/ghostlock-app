#ifndef GHOSTLOCK_VICTIM_CONTEXT_HPP
#define GHOSTLOCK_VICTIM_CONTEXT_HPP

#include "support/native_resource.hpp"

namespace ghostlock {

/* Owns the six pipe ends of the victim protocol. The child-side ends are
 * closed by the fork child before child_main runs; the parent-side ends are
 * closed once their protocol step is done. parked_cmd_w receives the cmd
 * write end by move when a rooted child is parked across W3 rounds.
 *
 * The child pid stays with the caller for now: a rooted victim that reads EOF
 * finishes the handoff on its own, so pid ownership needs its own gate. */
class VictimContext final {
 public:
  VictimContext() noexcept = default;
  VictimContext(const VictimContext &) = delete;
  VictimContext &operator=(const VictimContext &) = delete;

  UniqueFd task_read;
  UniqueFd cmd_write;
  UniqueFd uid_read;
  UniqueFd task_write;
  UniqueFd cmd_read;
  UniqueFd uid_write;
};

}  // namespace ghostlock

#endif
