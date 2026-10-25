/* Host fixed-vector test for the futex bucket hash.
 *
 * Locks the Jenkins hash mix and the power-of-two truncation used by
 * KernelSnitch collision discovery, plus the policy validation guards. The
 * vectors are the compatibility baseline: any change to the mixing constants,
 * key layout or mask must update them deliberately.
 */

#include "../kernelsnitch/futex_hash.h"

#include <cassert>
#include <cstdint>
#include <cstdio>


int32_t main(void) {
    static const struct {
        size_t table_size;
        size_t addr;
        size_t mm;
        uint32_t expected_key_hash;
        uint32_t expected_bucket;
    } vectors[] = {
        {
            0x100ULL, 0x7f0000001000ULL, 0xffffff8801234000ULL, 0x0000006cU,
            0x0000006cU
        },
        {
            0x100ULL, 0x7f0000001ffcULL, 0xffffff8801234000ULL, 0x000000eaU,
            0x000000eaU
        },
        {
            0x400ULL, 0x7f0000001000ULL, 0xffffff8801234000ULL, 0x0000016cU,
            0x0000016cU
        },
        {
            0x1000ULL, 0xdead0000beefULL, 0xffffff8c00000000ULL, 0x00000bbeU,
            0x00000bbeU
        },
    };
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        ghostlock::kernelsnitch::FutexHashContext context = {};
        assert(futex_hash_context_init(&context, vectors[i].table_size) == 0);
        assert(context.table_size == vectors[i].table_size);

        ghostlock::kernelsnitch::futex_key_t key = {};
        key.private_key.mm = (void *) vectors[i].mm;
        key.private_key.address = vectors[i].addr & ~(size_t) 0xfff;
        key.private_key.offset = (uint32_t) (vectors[i].addr & 0xfff);
        assert(futex_hash_context_key(&context, &key) ==
               vectors[i].expected_key_hash);
        assert(futex_hash_context_bucket(
                   &context, vectors[i].addr, vectors[i].mm) ==
               vectors[i].expected_bucket);
        assert(vectors[i].expected_bucket < vectors[i].table_size);
    }

    /* The mask is applied to the same untruncated hash for every table size. */
    ghostlock::kernelsnitch::FutexHashContext small = {};
    ghostlock::kernelsnitch::FutexHashContext large = {};
    assert(futex_hash_context_init(&small, 256) == 0);
    assert(futex_hash_context_init(&large, 4096) == 0);
    const uint32_t small_bucket = futex_hash_context_bucket(
        &small, 0x7f0000001000ULL, 0xffffff8801234000ULL);
    const uint32_t large_bucket = futex_hash_context_bucket(
        &large, 0x7f0000001000ULL, 0xffffff8801234000ULL);
    assert((large_bucket & 0xffU) == small_bucket);

    /* Invalid policy inputs are rejected without mutating the context. */
    ghostlock::kernelsnitch::FutexHashContext rejected = {};
    assert(futex_hash_context_init(&rejected, 0) == -1);
    assert(futex_hash_context_init(&rejected, 255) == -1);
    assert(futex_hash_context_init(&rejected, 300) == -1);
    assert(rejected.table_size == 0);
    if (sizeof(size_t) > sizeof(uint32_t)) {
        assert(futex_hash_context_init(&rejected, (size_t) 1 << 32) == -1);
    }
    assert(ghostlock::kernelsnitch::futex_hash_context_init(nullptr, 256) == -1);

    /* An uninitialized context cannot produce a bucket. */
    ghostlock::kernelsnitch::futex_key_t key = {};
    assert(futex_hash_context_key(&rejected, &key) == UINT32_MAX);
    assert(futex_hash_context_key(nullptr, &key) == UINT32_MAX);
    assert(futex_hash_context_bucket(&rejected, 0x1000, 0x2000) == UINT32_MAX);

    /* Kernel-equivalent table sizing: possible CPUs, rounded up. Hotplug can
   * leave the online count non-power-of-two, which must not fail the policy. */
    assert(ghostlock::kernelsnitch::futex_hash_table_size_for(1) == 256);
    assert(ghostlock::kernelsnitch::futex_hash_table_size_for(6) == 2048);
    assert(ghostlock::kernelsnitch::futex_hash_table_size_for(7) == 2048);
    assert(ghostlock::kernelsnitch::futex_hash_table_size_for(8) == 2048);
    assert(ghostlock::kernelsnitch::futex_hash_table_size_for(12) == 4096);
    ghostlock::kernelsnitch::FutexHashContext sized = {};
    assert(futex_hash_context_init(&sized, ghostlock::kernelsnitch::futex_hash_table_size_for(7)) == 0);
    assert(sized.table_size == 2048);

    puts("futex_hash_test: ok");
    return 0;
}
