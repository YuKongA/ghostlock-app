#pragma once
/** @file backend/cve_2026_43284/landing/pagecache_doors.hpp
 *  The second backend's doors, with the rule the old tree already kept.
 *
 *  Measured before writing: the binding layer opened the descriptor with close-on-exec, checked it,
 *  installed the splice face, and on its failure path closed what it had opened. So the rule is not
 *  a preference of this design -- whoever opens a door closes that door -- and a descriptor handed
 *  in by a caller must not be closed here, because closing someone else's is a double close and
 *  hurts more than leaving it.
 *
 *  The system calls sit behind a small interface for the same reason the route and the window do:
 *  what is pinned here is the bookkeeping -- which door was opened by whom, what happens when the
 *  second one fails, and what the closing leaves behind -- and that can be driven on the host.
 *
 *  What this deliberately does not decide: whether the target arrives as a path or as an already
 *  open descriptor. The ruling says values arrive from the script, so either is possible and the
 *  choice belongs to the parameter set; this file supports both and closes only the first kind.
 */
#include <cstdint>

#include "backend/cve_2026_43284/access_params.hpp"

/* 桥的形态：装配层的一个取值，不是第二个实现。默认 0 = 每块 fork 一次（今天的行为，逐字节不变）；
 * 显式 1 = 一次 fork 的常驻会话（bridge_session.hpp）。默认不动的原因写在 docs/analysis/
 * persistent-helper-bridge-design.md 与本批报告里：真机锁屏（2026-10-17），没有设备见证过的行为不上默认。
 * 切换点只有这里：门的取值、面的取值与桥的取值都读 kBridgeModeDefault，所以一条构建参数就能整链切换。 */
#ifndef GLK_BRIDGE_MODE
#define GLK_BRIDGE_MODE 0
#endif

namespace ghostlock::backend::cve_2026_43284::landing {
    /** 每块 fork 一次：现状，也是默认。 */
    inline constexpr int kBridgeModeSpawn = 0;
    /** 一次 fork、之后每块一次 IPC。 */
    inline constexpr int kBridgeModeSession = 1;
    /** 唯一的默认值。 */
    inline constexpr int kBridgeModeDefault =
        GLK_BRIDGE_MODE == 1 ? kBridgeModeSession : kBridgeModeSpawn;
    class PageCacheDoorLinux;
}  // namespace ghostlock::backend::cve_2026_43284::landing

namespace ghostlock::backend::cve_2026_43284::landing {
    /** The system calls the doors need. */
    class DoorOps {
    public:
        DoorOps() noexcept = default;
        DoorOps(const DoorOps &) = delete;
        DoorOps &operator=(const DoorOps &) = delete;
        virtual ~DoorOps() noexcept = default;

        /** Open the target for reading, or a negative value. */
        [[nodiscard]] virtual int open_target() noexcept = 0;
        /** Connect the ESP socket from the credentials, or a negative value. The credentials are
         *  the whole frame and not just the key, because the socket needs the ports as well. */
        [[nodiscard]] virtual int connect_esp(const SaMaterial &sa) noexcept = 0;
        /** Close a descriptor this object opened. */
        virtual void close_fd(int fd) noexcept = 0;
        /** Whether the carrier is served remotely: this process never opens it, a bridge reads and
         *  splices its page instead. False for every carrier this process can open itself. */
        [[nodiscard]] virtual bool target_is_remote() const noexcept { return false; }
    };

    /** The doors of one route lifetime: opened once, closed once, only by their opener. */
    class PageCacheDoors final {
    public:
        /**
         *  @param ops          The system calls the two doors need.
         *  @param handed_in_fd A carrier descriptor the caller already holds; used and never closed
         *                      here.
         *  @param bridge_mode  How this route serves a carrier it cannot open: kBridgeModeSpawn (the
         *                      default, one helper exec per block) or kBridgeModeSession (one exec,
         *                      asked over a channel). The window compares it with the surface's own
         *                      value when the route comes up, so a half-switched assembly is refused
         *                      at bring-up rather than paying a fork per block unnoticed.
         */
        PageCacheDoors(DoorOps &ops, int handed_in_fd = -1,
                       int bridge_mode = kBridgeModeDefault) noexcept
            : ops_(ops), handed_in_(handed_in_fd), bridge_mode_(bridge_mode) {}

        /** Open both doors. On failure nothing is left half open. */
        [[nodiscard]] bool open(const SaMaterial &sa) noexcept {
            if (open_) {
                return false;
            }
            /* A descriptor the caller handed in is used and NOT owned: it is never closed here. */
            file_fd_ = handed_in_;
            owns_file_ = false;
            /* A remote carrier has no descriptor at all, and that is not a failure: the bridges serve
             * both halves. Only a carrier this process is supposed to open can fail to open. */
            if (file_fd_ < 0 && !ops_.target_is_remote()) {
                file_fd_ = ops_.open_target();
                if (file_fd_ < 0) {
                    return false;
                }
                owns_file_ = true;
            }
            socket_fd_ = ops_.connect_esp(sa);
            if (socket_fd_ < 0) {
                /* The second door failed: give back the first rather than leave a half-open route. */
                close_owned();
                return false;
            }
            open_ = true;
            return true;
        }

        /** Close what this opened, and only that. */
        [[nodiscard]] bool close() noexcept {
            if (!open_) {
                return false;
            }
            close_owned();
            socket_fd_ = -1;
            open_ = false;
            return true;
        }

        [[nodiscard]] int file_fd() const noexcept { return file_fd_; }
        [[nodiscard]] int socket_fd() const noexcept { return socket_fd_; }
        /** Whether the file door is this object's to close. */
        [[nodiscard]] bool owns_file() const noexcept { return owns_file_; }
        /** 这条路由声明的桥形态（0 = spawn，1 = session）。 */
        [[nodiscard]] int bridge_mode() const noexcept { return bridge_mode_; }

    private:
        void close_owned() noexcept {
            if (owns_file_ && file_fd_ >= 0) {
                ops_.close_fd(file_fd_);
                file_fd_ = -1;
                owns_file_ = false;
            }
            if (socket_fd_ >= 0) {
                ops_.close_fd(socket_fd_);
                socket_fd_ = -1;
            }
        }

        DoorOps &ops_;
        int handed_in_ = -1;
        int file_fd_ = -1;
        int socket_fd_ = -1;
        int bridge_mode_ = kBridgeModeDefault;
        bool owns_file_ = false;
        bool open_ = false;
    };
/** 本进程从不打开的那种载体：两半都交给桥，门只留套接字那一半（工具与执行体共用这一份）。 */
    class RemoteDoor final : public DoorOps {
    public:
        explicit RemoteDoor(PageCacheDoorLinux &real) noexcept;
        [[nodiscard]] int open_target() noexcept override { return -1; }
        [[nodiscard]] int connect_esp(const SaMaterial &sa) noexcept override;
        void close_fd(int fd) noexcept override;
        [[nodiscard]] bool target_is_remote() const noexcept override { return true; }

    private:
        PageCacheDoorLinux *real_ = nullptr;
    };
}  // namespace ghostlock::backend::cve_2026_43284::landing
