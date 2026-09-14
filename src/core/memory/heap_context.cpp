#include "memory/heap_context.h"

#include <string.h>
#include <unistd.h>

static void payload_page_init(PayloadPage *page) {
    memset(page, 0, sizeof(*page));
    page->reclaim.fd[0] = -1;
    page->reclaim.fd[1] = -1;
    page->state = PAYLOAD_PAGE_EMPTY;
}

void heap_context_init(HeapContext *context) {
    memset(context, 0, sizeof(*context));
    context->leak_memfd = -1;
    payload_page_init(&context->current);
    payload_page_init(&context->prebuilt);
    payload_page_init(&context->quarantine);
}

int payload_page_has_reclaim(const PayloadPage *page) {
    return page && page->reclaim.fd[0] >= 0 && page->reclaim.fd[1] >= 0;
}

void payload_page_destroy(PayloadPage *page) {
    if (!page) return;
    for (int i = 0; i < 2; i++) {
        if (page->reclaim.fd[i] >= 0) close(page->reclaim.fd[i]);
    }
    payload_page_init(page);
}

int payload_page_move(
        PayloadPage *destination, PayloadPage *source,
        PayloadPageState destination_state) {
    if (!destination || !source || destination == source ||
            destination->state != PAYLOAD_PAGE_EMPTY ||
            !payload_page_has_reclaim(source)) {
        return 0;
    }
    *destination = *source;
    destination->state = destination_state;
    payload_page_init(source);
    return 1;
}
