#pragma once
/** @file jni/handle_registry.hpp
 *  会话句柄表（**不含 JNI**）：每个 backend 库一份，句柄 = 索引 + 代。
 *
 *  为什么把它单独放一个头文件：它的规则（并发单位是句柄、销毁先关闭、回收后句柄作废、槽复用要换代）
 *  是可以用宿主测试钉住的，而 JNI 外壳只需要"取数组、调这里、写回数组"。审查指出旧写法把机械与规则
 *  混在一起，结果 JNI 层没有任何测试 —— 分开之后规则可测，外壳只剩薄薄一层。
 *
 *  加锁顺序固定为「表 → 槽」，且 retire 先把槽标成 dead 再取槽锁：在飞的那一次调用会先跑完，
 *  之后才释放资源（不会出现"调用还在跑、对象已被清掉"）。
 */
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "jni/backend_api.hpp"

namespace ghostlock::jni {
    /**
     *  一个 backend 库的会话表。
     *
     *  @tparam Backend 该 backend 的会话类型（Backend43499 / Backend43284）。
     */
    template <class Backend>
    class HandleRegistry final {
      public:
        /** 设定新会话的模板：带缝的 backend 由它在建会话时复制进来。 */
        void set_prototype(Backend prototype) noexcept {
            std::lock_guard<std::mutex> table_guard(table_mutex_);
            prototype_ = std::move(prototype);
        }

        /** 建一个会话。@return 句柄（永远 > 0）。 */
        [[nodiscard]] std::uint64_t create() noexcept {
            std::lock_guard<std::mutex> table_guard(table_mutex_);
            for (std::size_t index = 0U; index < slots_.size(); ++index) {
                Slot &slot = slots_[index];
                if (!slot.live) {
                    slot.live = true;
                    slot.generation += 1U;
                    std::lock_guard<std::mutex> slot_guard(slot.mutex);
                    slot.backend = prototype_;
                    return make_handle(index, slot.generation);
                }
            }
            slots_.emplace_back();
            Slot &slot = slots_.back();
            slot.live = true;
            slot.generation = 1U;
            std::lock_guard<std::mutex> slot_guard(slot.mutex);
            slot.backend = prototype_;
            return make_handle(slots_.size() - 1U, 1U);
        }

        /**
         *  回收一个会话：**先关闭，再清状态**。
         *
         *  @return 是否回收成功（未知句柄 = false）。
         */
        [[nodiscard]] bool retire(const std::uint64_t handle) noexcept {
            Slot *slot = nullptr;
            {
                std::lock_guard<std::mutex> table_guard(table_mutex_);
                slot = find(handle);
                if (slot == nullptr) {
                    return false;
                }
                slot->live = false;
            }
            std::lock_guard<std::mutex> slot_guard(slot->mutex);
            static_cast<void>(slot->backend.shutdown());
            slot->backend = Backend{};
            return true;
        }

        /** 一次调用的答案：调用码与具名拒绝在**同一次加锁**里取到。 */
        struct Outcome final {
            CallCode code = CallCode::StaleHandle;
            char refusal[128] = {};
        };

        /**
         *  在一次调用里持有**这个句柄**的锁，并把答案成对取出来。
         *
         *  @param work 收到会话引用；返回调用码。
         *  @return 调用码 + 那次调用的具名拒绝（句柄无效时是 stale-handle）。
         */
        template <class Work>
        [[nodiscard]] Outcome with_answer(const std::uint64_t handle, Work &&work) noexcept {
            Outcome outcome{};
            Slot *slot = nullptr;
            {
                std::lock_guard<std::mutex> table_guard(table_mutex_);
                slot = find(handle);
            }
            if (slot == nullptr) {
                copy_name(outcome.refusal, "stale-handle");
                return outcome;
            }
            std::lock_guard<std::mutex> slot_guard(slot->mutex);
            if (!slot->live || slot->generation != (handle >> 32U)) {
                copy_name(outcome.refusal, "stale-handle");
                return outcome;
            }
            outcome.code = work(slot->backend);
            copy_name(outcome.refusal, slot->backend.refusal());
            return outcome;
        }

        /** @return 当前活着的会话数（诊断与测试用）。 */
        [[nodiscard]] std::size_t live_count() noexcept {
            std::lock_guard<std::mutex> table_guard(table_mutex_);
            std::size_t live = 0U;
            for (const Slot &slot : slots_) {
                if (slot.live) {
                    ++live;
                }
            }
            return live;
        }

      private:
        struct Slot final {
            std::mutex mutex{};
            bool live = false;
            std::uint64_t generation = 0U;
            Backend backend{};
        };

        /** 把一条名字拷进定长缓冲（截断而不是溢出）。 */
        static void copy_name(char *out, const char *name) noexcept {
            std::size_t at = 0U;
            while (name[at] != '\0' && at + 1U < 128U) {
                out[at] = name[at];
                ++at;
            }
            out[at] = '\0';
        }

        [[nodiscard]] static std::uint64_t make_handle(const std::size_t index,
                                                       const std::uint64_t generation) noexcept {
            /* 1 起编：0 留给"没有句柄"。 */
            return (generation << 32U) | (static_cast<std::uint64_t>(index) + 1U);
        }

        /** @return 该句柄指向的槽，或 null（不存在 / 已回收 / 代不对）。调用方持表锁。 */
        [[nodiscard]] Slot *find(const std::uint64_t handle) noexcept {
            const std::uint64_t index = (handle & 0xFFFFFFFFU) - 1U;
            const std::uint64_t generation = handle >> 32U;
            if (handle == 0U || index >= slots_.size()) {
                return nullptr;
            }
            Slot &slot = slots_[index];
            if (!slot.live || slot.generation != generation) {
                return nullptr;
            }
            return &slot;
        }

        std::mutex table_mutex_{};
        /* deque：Slot 里有 mutex，不可搬；deque 增长不会挪动已存在的元素。 */
        std::deque<Slot> slots_{};
        Backend prototype_{};
    };
}  // namespace ghostlock::jni
