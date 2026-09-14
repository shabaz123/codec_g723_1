#include "g723_basicop.h"

uint32_t g723_isqrt64(uint64_t v) {
    if (v == 0) {
        return 0;
    }
    uint64_t x = v;
    uint64_t r = 0;
#if defined(__GNUC__) || defined(__clang__)
    int lz = __builtin_clzll(v);
#else
    int lz = 0;
    uint64_t temp = v;
    while ((temp & (1ULL << 63)) == 0) {
        lz++;
        temp <<= 1;
    }
#endif
    uint64_t shift = (62 - (lz & ~1));
    uint64_t bit = 1ULL << shift;

    while (bit != 0) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}
