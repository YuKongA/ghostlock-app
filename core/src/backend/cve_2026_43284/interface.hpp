#pragma once

#include <cstddef>
#include <cstdint>

/**
 * 43284 重置后的抽象面：只有 POD，没有 KernelAccess、没有会话、没有实现。
 * 具体实现按上游源码逐步重写；JNI 只做薄导出。
 */
namespace ghostlock::backend::cve_2026_43284 {

struct AccessParams {
    const char *route = nullptr;
    std::uint64_t offset = 0;
    bool offset_set = false;
};

struct SaMaterial {
    std::uint32_t spi = 0;
    std::uint32_t sequence = 1;
    std::uint8_t aes_key[32] = {};
};

struct PageCacheWriteRequest {
    const char *path = nullptr;
    std::uint64_t offset = 0;
    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
};

}  // namespace ghostlock::backend::cve_2026_43284
