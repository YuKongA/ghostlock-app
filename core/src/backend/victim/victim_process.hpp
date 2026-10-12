#pragma once
/** @file backend/victim/victim_process.hpp
 *  受害者进程协议（最小可行子集，照退役 \`victim/victim_process.{hpp,cpp}\`）：
 *  fork 一个受害者、在**子侧**用 perf 泄漏它自己的 task_struct、把地址送回父侧；
 *  拿到 root 后由父侧命令它**永久驻留**（退出会 kfree 静态 init_cred）。
 *
 *  本批只带 w2 需要的三条：报告 uid（\`C\` ✓）、驻留（\`P\` ✓）、交接（\`G\`/\`X\` ✓）；
 *  w3 的 finit_module 探针与 comm 报告（\`F\`/\`M\`）留待需要时 ✓。
 *
 *  设备专用。
 */
#include <cstdint>
#include <sys/types.h>

#include "backend/victim/victim_context.hpp"

namespace ghostlock::backend::victim {
    /** 一次 fork 的结果：pid 与**子侧泄漏并两次一致**的 task 地址。 */
    struct VictimSpawn final {
        pid_t pid = -1;
        std::uint64_t task = 0U;
    };

    /** 失败名（具名，别让调用方猜）：管道 / fork / 泄漏不一致。 */
    enum class SpawnRefusal : unsigned char { Ok = 0, Pipes = 1, Fork = 2, LeakDisagreed = 3 };

    /** Fork the victim, read back the task pointer the child's perf leak agreed on.
     *  @return Ok 或一个具名拒绝；任何拒绝都**不留子进程** ✓。 */
    [[nodiscard]] SpawnRefusal spawn_victim(VictimContext &context, VictimSpawn &out) noexcept;

    /** 让父侧问子侧要它的 uid（拿到 root 的判据通道 ✓）。 */
    [[nodiscard]] bool victim_report_uid(VictimContext &context, std::uint32_t &uid) noexcept;

    /** 问子侧要它的 comm 长度与首字节（旧码 M 通道：用来分辨"写落在哪一侧"——
     *  comm 被清 ⇒ 长度 0；被写后 8 字节 ⇒ 长度 8；未动 ⇒ 15）。 */
    [[nodiscard]] bool victim_report_comm(VictimContext &context, std::uint32_t &report) noexcept;

    /** 命令子侧永久驻留（\`oom_score_adj = -1000\`；**不得**让它退出 ✓）。 */
    [[nodiscard]] bool victim_park(VictimContext &context) noexcept;

    /** 交接：子侧自己 exec root 脚本那条路（本批只用它来"放走"pid ✓）。 */
    [[nodiscard]] bool victim_handoff(VictimContext &context) noexcept;
}  // namespace ghostlock::backend::victim
