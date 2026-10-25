/**
 * @file payload_vectors.cpp
 * @brief multicast 载荷编码器的 host 向量（设备无关的可执行判据）。
 *
 * 用途：把「264 字节 stamp 布局对不对」从"只能靠真机 W1"变成秒级可重复的判据。
 * 冻结值来自当前 C 实现（`payload_encode.h`），Kotlin 侧构造器必须与它逐字节一致。
 *
 * 断言分三类：
 *  1. FNV-1a-64 整段哈希 = 冻结值（逐字节等价，最严格）；
 *  2. 语义：`task`/`lock` 落在 `waiter + offset`，其余字节为零，编码器不碰 family 槽；
 *  3. 边界：`len` 恰好/差一、越界 offset、零长度 —— 必须返回 false 且不越界写。
 *
 * 构建与运行（host，无需 NDK / 无需设备 / 无需 Android 头）：
 *
 *   c++ -std=c++23 -I <repo>/app/src/main/cpp/gl43499/core \
 *       <repo>/app/src/main/cpp/gl43499/core/tests/host/payload_vectors.cpp \
 *       -o /tmp/payload_vectors && /tmp/payload_vectors; echo $?
 *
 * 预期：全部 PASS，退出码 0。
 */
#include "payload_encode.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace {
    int g_failed = 0;

    void check(bool ok, const char *what, const char *detail = "") {
        if (ok) {
            std::printf("PASS %s\n", what);
            return;
        }
        ++g_failed;
        std::printf("FAIL %s %s\n", what, detail);
    }

    /** @brief FNV-1a 64；整段哈希作为逐字节等价的紧凑冻结值。 */
    std::uint64_t fnv1a64(const unsigned char *p, std::size_t n) {
        std::uint64_t h = 1469598103934665603ULL;
        for (std::size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::uint64_t load64le(const unsigned char *p) {
        std::uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
        return v;
    }

    struct Vector {
        const char *name;
        std::size_t waiter;
        std::size_t task;
        std::size_t lock;
        std::size_t len;
        std::uintptr_t fake_task;
        std::uintptr_t fake_lock;
        std::uint64_t frozen_fnv;
    };

    /* 冻结自 2026-10-25 的 C 实现；改动编码器语义必须同时更新这里并说明理由。 */
    constexpr Vector k_vectors[] = {
        {"A_5_15", 96, 48, 56, 264, 0xffffff8963a7a400ULL, 0xffffff8963a7a000ULL,
         0x22c8954c1abaefc7ULL},
        {"B_alt", 64, 32, 40, 128, 0xdeadbeef12345678ULL, 0x1122334455667788ULL,
         0x106f733019f41245ULL},
    };

    /** @brief family 槽（`group_source_req.gsr_group.ss_family`）由路由层写，编码器不得触碰。 */
    constexpr std::size_t k_family_offset = 8;

    void run_vector(const Vector &v) {
        std::vector<unsigned char> buf(v.len, 0);
        const bool ok = ghostlock::memory::encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(buf.data()), buf.size()}, v.waiter, v.task,
            v.lock, v.fake_task, v.fake_lock);
        char tag[64];
        std::snprintf(tag, sizeof(tag), "%s encode-ok", v.name);
        check(ok, tag);

        std::snprintf(tag, sizeof(tag), "%s fnv-frozen", v.name);
        check(fnv1a64(buf.data(), buf.size()) == v.frozen_fnv, tag);

        std::snprintf(tag, sizeof(tag), "%s task-field", v.name);
        check(load64le(buf.data() + v.waiter + v.task) == v.fake_task, tag);

        std::snprintf(tag, sizeof(tag), "%s lock-field", v.name);
        check(load64le(buf.data() + v.waiter + v.lock) == v.fake_lock, tag);

        /* 除两个字段外全零，且 family 槽仍为零（编码器不写它）。 */
        bool zeros = true;
        for (std::size_t i = 0; i < buf.size(); ++i) {
            const bool task_byte = i >= v.waiter + v.task && i < v.waiter + v.task + 8;
            const bool lock_byte = i >= v.waiter + v.lock && i < v.waiter + v.lock + 8;
            if (!task_byte && !lock_byte && buf[i] != 0) {
                zeros = false;
                break;
            }
        }
        std::snprintf(tag, sizeof(tag), "%s others-zero+family-untouched", v.name);
        check(zeros, tag);

        /* 边界：恰好够 / 差一字节。 */
        const std::size_t needed =
            v.waiter + (v.task > v.lock ? v.task : v.lock) + sizeof(std::uint64_t);
        std::vector<unsigned char> exact(needed, 0);
        std::snprintf(tag, sizeof(tag), "%s len=needed accepted", v.name);
        check(ghostlock::memory::encode_multicast_waiter(
                  {reinterpret_cast<std::byte *>(exact.data()), exact.size()}, v.waiter,
                  v.task, v.lock, v.fake_task, v.fake_lock),
              tag);

        if (needed > 0) {
            std::vector<unsigned char> short_by_one(needed - 1, 0);
            std::snprintf(tag, sizeof(tag), "%s len=needed-1 rejected", v.name);
            check(!ghostlock::memory::encode_multicast_waiter(
                      {reinterpret_cast<std::byte *>(short_by_one.data()),
                       short_by_one.size()},
                      v.waiter, v.task, v.lock, v.fake_task, v.fake_lock),
                  tag);
        }
    }
} // namespace

int main() {
    for (const Vector &v : k_vectors) run_vector(v);

    /* 零长度缓冲：任何写入都必须被拒绝。 */
    check(!ghostlock::memory::encode_multicast_waiter({}, 0, 0, 0, 1, 2),
          "len=0 rejected");

    /* waiter 起点已越界。 */
    std::vector<unsigned char> tiny(16, 0);
    check(!ghostlock::memory::encode_multicast_waiter(
              {reinterpret_cast<std::byte *>(tiny.data()), tiny.size()}, 32, 0, 0, 1, 2),
          "waiter-past-end rejected");

    /* lock 越界：task 写得进去、lock 写不进去 —— 必须整体返回 false。 */
    std::vector<unsigned char> partial(160, 0);
    check(!ghostlock::memory::encode_multicast_waiter(
              {reinterpret_cast<std::byte *>(partial.data()), partial.size()}, 96, 48, 57, 7,
              8),
          "lock-overflow rejected");

    /* family 槽在最小合法缓冲里也不被触碰（见 k_family_offset）。 */
    std::vector<unsigned char> fam(264, 0xFF);
    std::memset(fam.data(), 0, fam.size());
    (void) ghostlock::memory::encode_multicast_waiter(
        {reinterpret_cast<std::byte *>(fam.data()), fam.size()}, 96, 48, 56, 1, 2);
    check(fam[k_family_offset] == 0 && fam[k_family_offset + 1] == 0,
          "family slot untouched");

    std::printf("RESULT %s (%d failed)\n", g_failed == 0 ? "PASS" : "FAIL", g_failed);
    return g_failed == 0 ? 0 : 1;
}
