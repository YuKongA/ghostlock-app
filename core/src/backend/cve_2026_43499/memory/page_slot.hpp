#pragma once
/** @file backend/cve_2026_43499/memory/page_slot.hpp
 *  W2「预建页」的一次性槽（见 docs/analysis/43499-goal12-plan-prebuilt-page.md ✓）。
 *
 *  退役把"为修复请求准备好的页"在 **prebuild → 主写 → activate** 之间**存起来**再取用
 *  （`multicast_waiter_route.cpp:170/187` 的 `stash_prebuilt_page` / `activate_prebuilt_page` ✓）；
 *  本文件只提供**槽**本身：不拥有字节（由调用方持有 ✓）、不分配 ✗、无全局状态 ✗。
 *
 *  @note 一次性：`activate` 成功后槽即空 ⇒ 第二次 `activate` 必失败（具名语义、不假装成功）。
 *  @note 生命周期（守则 §5）：成功/失败/重试/取消**每条出口**都必须 `discard` ✓。
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** The one-shot slot an already built page waits in. */
    struct PageSlot final {
        /** Null until `stash` accepts a page, null again after the slot is taken or given back. */
        const std::uint8_t *bytes = nullptr;
        /** The stashed page length, zero whenever `bytes` is null. */
        std::size_t size = 0U;

        /** @return True while a stashed page is waiting to be taken. */
        [[nodiscard]] bool staged() const noexcept { return bytes != nullptr && size != 0U; }

        /** Put a built page in the slot.
         *  @param page   The caller-owned page bytes.
         *  @param length How many bytes the page occupies.
         *  @return False when the slot is occupied already, or the page is empty. */
        [[nodiscard]] bool stash(const std::uint8_t *page, std::size_t length) noexcept {
            if (staged() || page == nullptr || length == 0U) {
                return false;
            }
            bytes = page;
            size = length;
            return true;
        }

        /** Take the stashed page out of the slot, once.
         *  @param page   Receives the page bytes; untouched on refusal.
         *  @param length Receives the page length; untouched on refusal.
         *  @return False when nothing was stashed (the slot is left empty). */
        [[nodiscard]] bool activate(const std::uint8_t *&page, std::size_t &length) noexcept {
            if (!staged()) {
                return false;
            }
            page = bytes;
            length = size;
            discard();
            return true;
        }

        /** Give the slot back; safe to call on an empty slot and more than once. */
        void discard() noexcept {
            bytes = nullptr;
            size = 0U;
        }
    };
} // namespace ghostlock::backend::cve_2026_43499::memory
