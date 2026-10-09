#pragma once
/** @file backend/cve_2026_43284/seam/pagecache_doors.hpp
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

namespace ghostlock::backend::cve_2026_43284::seam {
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
        [[nodiscard]] virtual int connect_esp(const AccessParams &sa) noexcept = 0;
        /** Close a descriptor this object opened. */
        virtual void close_fd(int fd) noexcept = 0;
    };

    /** The doors of one route lifetime: opened once, closed once, only by their opener. */
    class PageCacheDoors final {
    public:
        PageCacheDoors(DoorOps &ops, int handed_in_fd = -1) noexcept : ops_(ops), handed_in_(handed_in_fd) {}

        /** Open both doors. On failure nothing is left half open. */
        [[nodiscard]] bool open(const AccessParams &sa) noexcept {
            if (open_) {
                return false;
            }
            /* A descriptor the caller handed in is used and NOT owned: it is never closed here. */
            file_fd_ = handed_in_;
            owns_file_ = false;
            if (file_fd_ < 0) {
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
        bool owns_file_ = false;
        bool open_ = false;
    };
}  // namespace ghostlock::backend::cve_2026_43284::seam
