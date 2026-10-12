/** @file tests/encap_peer_test.cpp
 *  The ESP-in-UDP peer: what it says about each way of opening it.
 *
 *  判据：端口为 0 ⇒ 具名失败；同一端口开两次 ⇒ 第二次报「别人持有」；平台上没有 ESP-in-UDP 时
 *  按「用不着」处理（不是失败）。名字与顺序是接口的一部分，一并钉住。
 */
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43284/seam/encap_peer.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::seam::EncapPeer;

    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    void the_names_are_stable() {
        expect(std::strcmp(EncapPeer::status_name(EncapPeer::Status::Never), "never") == 0, "never");
        expect(std::strcmp(EncapPeer::status_name(EncapPeer::Status::Bound), "bound") == 0, "bound");
        expect(std::strcmp(EncapPeer::status_name(EncapPeer::Status::AlreadyHeld), "already-held") == 0,
               "already-held");
        expect(std::strcmp(EncapPeer::status_name(EncapPeer::Status::Failed), "failed") == 0, "failed");
    }

    void port_zero_is_refused() {
        EncapPeer peer{};
        expect(!peer.open(0U), "port zero is refused");
        expect(peer.status() == EncapPeer::Status::Failed, "and it says failed");
    }

    void the_same_port_twice_is_not_a_failure() {
        EncapPeer first{};
        EncapPeer second{};
        const std::uint16_t port = 39444U;   /* the port the live session used in the field notes */
        const bool opened = first.open(port);
        expect(opened, "the first peer opens (or the platform has no ESP-in-UDP and says so)");
        if (first.status() == EncapPeer::Status::Bound) {
            expect(second.open(port), "a second peer on a held port is not a failure");
            expect(second.status() == EncapPeer::Status::AlreadyHeld, "and it says already-held");
        } else {
            std::printf("  [note] this platform has no ESP-in-UDP: the bind path is not exercised here\n");
            expect(first.status() == EncapPeer::Status::AlreadyHeld,
                   "a platform without the option reports already-held, not failed");
        }
        second.close();
        first.close();
        expect(first.fd() < 0, "closing gives the descriptor back");
    }

    void closing_twice_is_fine() {
        EncapPeer peer{};
        peer.close();
        peer.close();
        expect(peer.fd() < 0, "closing twice is safe");
    }
}  // namespace

int main() {
    std::printf("encap peer: the names");
    the_names_are_stable();
    std::printf("encap peer: the refusals");
    port_zero_is_refused();
    std::printf("encap peer: the port that is already held");
    the_same_port_twice_is_not_a_failure();
    std::printf("encap peer: closing");
    closing_twice_is_fine();
    if (failures != 0) {
        std::printf("encap peer: FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("encap peer: PASS (names, refusals, a held port, closing)\n");
    return 0;
}
