/**
 * @file payload_encode.h
 * @brief multicast 载荷的纯编码器（无平台依赖，可在 host 上编译）。
 *
 * 从 `memory.cpp` 抽出：只依赖 `<span>` / `<cstring>` / `<cstdint>`，不引入
 * `linux/futex.h` 之类平台头，因此 host 向量测试（`core/tests/host/`）可以直接
 * 编译本头，而不必编整个 `memory.cpp`。
 *
 * @note 双实现期：本头是 C 侧的金标准来源，Kotlin 侧构造器必须与它逐字节一致。
 *       载荷形状迁到 Kotlin 且 host 向量冻结后，本头与两个函数一并删除。
 * @note 阶段：P-A（载荷外置）过渡物。
 */
#ifndef GHOSTLOCK_CORE_PAYLOAD_ENCODE_H
#define GHOSTLOCK_CORE_PAYLOAD_ENCODE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace ghostlock::core {
    /**
     * @brief 在 span 的指定偏移写一个小端 64 位值。
     *
     * @param bytes  目标缓冲；越界即拒绝，不写任何字节。
     * @param offset 字节偏移。
     * @param value  写入值（按宿主字节序，目标平台为小端）。
     * @return 写入成功为 true；越界为 false。
     */
    inline bool span_store64(std::span<std::byte> bytes, std::size_t offset,
                             std::uint64_t value) noexcept {
        if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
            return false;
        }
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
        return true;
    }
} // namespace ghostlock::core

namespace ghostlock::memory {
    /**
     * @brief 把 multicast waiter 的两个指针写进 stamp 缓冲。
     *
     * 布局：`fake_task` 落在 `waiter_offset + task_offset`，`fake_lock` 落在
     * `waiter_offset + lock_offset`；其余字节由调用方清零，本函数不碰。
     *
     * @param buffer        目标缓冲（整段 stamp，含 waiter 及其后续字段）。
     * @param waiter_offset waiter 结构在 buffer 内的起点。
     * @param task_offset   `task` 字段相对 waiter 的偏移。
     * @param lock_offset   `lock` 字段相对 waiter 的偏移。
     * @param fake_task     写入 `task` 的地址。
     * @param fake_lock     写入 `lock` 的地址。
     * @return 两处都写入成功为 true；任一处越界为 false（此时可能已写入前一处）。
     */
    inline bool encode_multicast_waiter(std::span<std::byte> buffer,
                                        std::size_t waiter_offset,
                                        std::size_t task_offset,
                                        std::size_t lock_offset,
                                        std::uintptr_t fake_task,
                                        std::uintptr_t fake_lock) noexcept {
        return core::span_store64(buffer, waiter_offset + task_offset, fake_task) &&
               core::span_store64(buffer, waiter_offset + lock_offset, fake_lock);
    }
} // namespace ghostlock::memory

#endif // GHOSTLOCK_CORE_PAYLOAD_ENCODE_H
