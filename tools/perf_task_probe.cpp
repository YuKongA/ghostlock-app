#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>

static int perf_open(perf_event_attr *attr, pid_t pid) {
    return static_cast<int>(syscall(SYS_perf_event_open, attr, pid, -1, -1, 0));
}

int main() {
    perf_event_attr attr{};
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.sample_period = 5000;
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_REGS_INTR;
    attr.sample_regs_intr = 0xffffffffULL;
    attr.disabled = 1;
    attr.exclude_kernel = 0;
    attr.exclude_user = 0;
    attr.wakeup_events = 1;
    const int fd = perf_open(&attr, 0);
    if (fd < 0) { std::printf("perf_open errno=%d\n", errno); return 1; }
    const long page = sysconf(_SC_PAGESIZE);
    const size_t ring_len = static_cast<size_t>(page) * 9U;
    void *ring = mmap(nullptr, ring_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) { std::printf("mmap errno=%d\n", errno); return 2; }
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    const timespec start{};
    (void)start;
    for (volatile int i = 0; i < 100000000; ++i) {}
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    auto *meta = static_cast<perf_event_mmap_page *>(ring);
    const uint64_t head = meta->data_head;
    const uint64_t tail = meta->data_tail;
    std::printf("head=%llu tail=%llu\n", (unsigned long long)head, (unsigned long long)tail);
    std::vector<uint64_t> candidates;
    uint64_t pos = tail;
    while (pos + 8 <= head) {
        const auto *base = static_cast<const unsigned char *>(ring) + page;
        const size_t off = static_cast<size_t>(pos % (ring_len - page));
        perf_event_header h{};
        std::memcpy(&h, base + off, sizeof(h));
        if (h.size == 0 || pos + h.size > head) break;
        if (h.type == PERF_RECORD_SAMPLE && h.size >= 8 + 16) {
            const unsigned char *p = base + off + 8;
            uint64_t ip = 0, abi = 0;
            std::memcpy(&ip, p, 8); p += 8;
            std::memcpy(&abi, p, 8); p += 8;
            std::printf("sample size=%u abi=%llu ip=%llx\n", h.size, (unsigned long long)abi, (unsigned long long)ip);
            if (abi == 1 || abi == 2) {
                for (int i = 0; i < 32 && p + 8 <= base + off + h.size; ++i) {
                    uint64_t reg = 0; std::memcpy(&reg, p, 8); p += 8;
                    const uint64_t canonical = reg | 0x0f00000000000000ULL;
                    if (candidates.size() < 40U) std::printf("reg[%d]=%llx\n", i, (unsigned long long)reg);
                    if (canonical > 0xffffff8000000000ULL) candidates.push_back(canonical);
                }
            }
        }
        pos += h.size;
    }
    std::sort(candidates.begin(), candidates.end());
    for (size_t i = 0; i < candidates.size(); ++i) {
        size_t n = 0; for (uint64_t v : candidates) if (v == candidates[i]) ++n;
        if (n >= 2) std::printf("cand %llx x%zu\n", (unsigned long long)candidates[i], n);
    }
    std::printf("candidates=%zu\n", candidates.size());
    return 0;
}
