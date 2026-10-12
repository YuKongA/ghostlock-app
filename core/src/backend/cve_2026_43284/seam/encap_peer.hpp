#pragma once
/** @file backend/cve_2026_43284/seam/encap_peer.hpp
 *  ESP-in-UDP 的接收侧套接字：内核的 ESP-in-UDP 处理挂在持有 UDP_ENCAP_ESPINUDP 的那个套接字上；
 *  没有它，发往该端口的报文只会被当成普通 UDP（真机上表现为 send 报 ECONNREFUSED）。
 *
 *  生产环境里这个套接字由 App 侧的 IpSec 会话持有，本进程不该也无法重复绑定（EADDRINUSE）。
 *  自建 SA 的实验路线下没人持有它，于是由本进程自己在 SA 的 dport 上开一个。
 *
 *  边界：只负责让它存在。收到的内容不由这里处理——页缓存的写入发生在发送那一侧（splice）。
 */
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::seam {
    /** One ESP-in-UDP receiver, held for as long as the route is up. */
    class EncapPeer final {
      public:
        EncapPeer() noexcept = default;
        EncapPeer(const EncapPeer &) = delete;
        EncapPeer &operator=(const EncapPeer &) = delete;
        ~EncapPeer() noexcept;

        /** How the last open ended. */
        enum class Status : unsigned char {
            Never = 0,
            /** 本进程现在持有它。 */
            Bound = 1,
            /** 已经有人持有（生产环境里就是 App 的会话）：不是错误。 */
            AlreadyHeld = 2,
            /** 开不出来：errno 在 error() 里。 */
            Failed = 3,
        };

        /** Bind @p port for ESP-in-UDP. @return true when the peer is usable (Bound or AlreadyHeld). */
        [[nodiscard]] bool open(std::uint16_t port) noexcept;
        /** Give the socket back (idempotent). */
        void close() noexcept;

        [[nodiscard]] Status status() const noexcept { return status_; }
        [[nodiscard]] int error() const noexcept { return error_; }
        [[nodiscard]] int fd() const noexcept { return fd_; }
        /** The name for a report line. */
        [[nodiscard]] static const char *status_name(Status status) noexcept;

      private:
        int fd_ = -1;
        Status status_ = Status::Never;
        int error_ = 0;
    };
}  // namespace ghostlock::backend::cve_2026_43284::seam
