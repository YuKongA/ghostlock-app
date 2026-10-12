#pragma once
/** @file backend/victim/victim_context.hpp
 *  受害者进程协议里**属于一次 W2 链**的那些资源：六个管道端 + 子进程 pid。
 *
 *  照退役树逐条（\`src/core/backend/victim/victim_context.{hpp,cpp}\`）：
 *   * 六个管道端（task/cmd/uid 各一对 ✓）；子侧端在 fork 出的子进程里、child_main 之前关掉 ✓；
 *   * **生命周期永远是显式的**（退役注释原文："Scope exit never signals the child … retirement is
 *     always explicit (retire_child()) and handoff transfers the pid away (release_child())"）；
 *     ⇒ 本类**不**在析构里收尾 ✓；
 *   * 拿到 root 的子进程要**永久驻留**（退出会 kfree 静态 init_cred ✓）⇒ 驻留由调用方负责 ✓。
 *
 *  设备专用（fork/pipe/signal）；主机上同名测试用同一份逻辑（pipe() 可移植 ✓）。
 */
#include <cstdint>
#include <sys/types.h>

namespace ghostlock::backend::victim {
    /** One W2/W3 chain's filesystem state: the six pipe ends and the child's pid. */
    class VictimContext final {
      public:
        VictimContext() noexcept = default;
        ~VictimContext() noexcept = default;
        VictimContext(const VictimContext &) = delete;
        VictimContext &operator=(const VictimContext &) = delete;

        /** 三个通道各一对端；父侧用其中属于父的那一半 ✓。 */
        std::int32_t task_read = -1;
        std::int32_t task_write = -1;
        std::int32_t cmd_read = -1;
        std::int32_t cmd_write = -1;
        std::int32_t uid_read = -1;
        std::int32_t uid_write = -1;

        [[nodiscard]] pid_t child() const noexcept { return child_; }
        void set_child(pid_t child) noexcept { child_ = child; }
        /** waitpid() 已回收该子进程：只丢 pid，不再向它发信号。 */
        void mark_child_exited() noexcept { child_ = -1; }
        /** 把 pid 交给驻留方/交接方（本上下文不再拥有它）。 */
        [[nodiscard]] pid_t release_child() noexcept {
            const pid_t taken = child_;
            child_ = -1;
            return taken;
        }
        /** 显式 SIGKILL + 回收（只对仍归本上下文所有的子进程）。 */
        void retire_child() noexcept;
        /** 关掉全部六个端（幂等 ✓；失败路径与成功路径都该走到 ✓）。 */
        void close_ends() noexcept;

        /** 六个端是否都开着（测试与调用方判断状态用）。 */
        [[nodiscard]] bool ends_open() const noexcept {
            return task_read >= 0 && task_write >= 0 && cmd_read >= 0 && cmd_write >= 0 &&
                   uid_read >= 0 && uid_write >= 0;
        }

      private:
        pid_t child_ = -1;
    };
}  // namespace ghostlock::backend::victim
