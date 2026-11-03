#include "../kernelsnitch/scan_bounds.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace ghostlock;


int32_t main(void) {
    assert(ghostlock::kernelsnitch::scan_limit(100, 20, 200) == 120);
    assert(ghostlock::kernelsnitch::scan_limit(100, 200, 150) == 150);
    assert(ghostlock::kernelsnitch::scan_limit(150, 20, 150) == 150);
    assert(ghostlock::kernelsnitch::scan_limit(SIZE_MAX - 8, 32, SIZE_MAX) == SIZE_MAX);
    puts("kernelsnitch_scan_bounds_test: ok");
    return 0;
}
