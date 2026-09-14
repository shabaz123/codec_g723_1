#ifndef G723_BASICOP_H
#define G723_BASICOP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int16_t g723_saturate16(int32_t x) {
    if (x > 32767) {
        return 32767;
    } else if (x < -32768) {
        return -32768;
    } else {
        return (int16_t)x;
    }
}

static inline int16_t g723_add(int16_t a, int16_t b) {
    return g723_saturate16((int32_t)a + (int32_t)b);
}

static inline int16_t g723_sub(int16_t a, int16_t b) {
    return g723_saturate16((int32_t)a - (int32_t)b);
}

static inline int16_t g723_abs_s(int16_t a) {
    if (a == -32768) {
        return 32767;
    } else if (a < 0) {
        return (int16_t)(-a);
    } else {
        return a;
    }
}

static inline int16_t g723_negate(int16_t a) {
    if (a == -32768) {
        return 32767;
    } else {
        return (int16_t)(-a);
    }
}

static inline int16_t g723_mult(int16_t a, int16_t b) {
    return g723_saturate16(((int32_t)a * (int32_t)b) >> 15);
}

static inline int16_t g723_mult_r(int16_t a, int16_t b) {
    return g723_saturate16(((int32_t)a * (int32_t)b + (1 << 14)) >> 15);
}

static inline int16_t g723_shr(int16_t a, int32_t n);

static inline int16_t g723_shl(int16_t a, int32_t n) {
    if (n <= 0) {
        return g723_shr(a, -n);
    }
    if (n >= 15) {
        return (a > 0) ? 32767 : ((a < 0) ? -32768 : 0);
    }
    return g723_saturate16(((int32_t)a) << n);
}

static inline int16_t g723_shr(int16_t a, int32_t n) {
    if (n < 0) {
        return g723_shl(a, -n);
    }
    if (n >= 15) {
        return (a < 0) ? -1 : 0;
    }
    return (int16_t)(a >> n);
}

static inline int32_t g723_l_add(int32_t a, int32_t b) {
    int64_t res = (int64_t)a + (int64_t)b;
    if (res > INT32_MAX) {
        return INT32_MAX;
    } else if (res < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)res;
}

static inline int32_t g723_l_sub(int32_t a, int32_t b) {
    int64_t res = (int64_t)a - (int64_t)b;
    if (res > INT32_MAX) {
        return INT32_MAX;
    } else if (res < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)res;
}

static inline int32_t g723_l_abs(int32_t a) {
    if (a == INT32_MIN) {
        return INT32_MAX;
    } else if (a < 0) {
        return -a;
    } else {
        return a;
    }
}

static inline int32_t g723_l_mult(int16_t a, int16_t b) {
    int32_t p = (int32_t)a * (int32_t)b;
    if (p == 0x40000000) {
        return INT32_MAX;
    } else {
        return p << 1;
    }
}

static inline int32_t g723_l_mac(int32_t acc, int16_t a, int16_t b) {
    return g723_l_add(acc, g723_l_mult(a, b));
}

static inline int32_t g723_l_msu(int32_t acc, int16_t a, int16_t b) {
    return g723_l_sub(acc, g723_l_mult(a, b));
}

static inline int32_t g723_l_deposit_h(int16_t a) {
    return ((int32_t)a) << 16;
}

static inline int32_t g723_l_deposit_l(int16_t a) {
    return (int32_t)a;
}

static inline int16_t g723_extract_h(int32_t a) {
    return (int16_t)(a >> 16);
}

static inline int16_t g723_extract_l(int32_t a) {
    return (int16_t)a;
}

static inline int16_t g723_round16(int32_t a) {
    return g723_extract_h(g723_l_add(a, 0x8000));
}

static inline int32_t g723_l_shr(int32_t a, int32_t n);

static inline int32_t g723_l_shl(int32_t a, int32_t n) {
    if (n <= 0) {
        return g723_l_shr(a, -n);
    }
    if (n >= 31) {
        return (a > 0) ? INT32_MAX : ((a < 0) ? INT32_MIN : 0);
    }
    int64_t res = (int64_t)a << n;
    if (res > INT32_MAX) {
        return INT32_MAX;
    } else if (res < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)res;
}

static inline int32_t g723_l_shr(int32_t a, int32_t n) {
    if (n < 0) {
        return g723_l_shl(a, -n);
    }
    if (n >= 31) {
        return (a < 0) ? -1 : 0;
    }
    return a >> n;
}

static inline int32_t g723_norm_s(int16_t a) {
    if (a == 0) {
        return 0;
    }
    int32_t x = (a < 0) ? ~(int32_t)a : (int32_t)a;
    int32_t n = 0;
    while (x < 0x4000 && n < 15) {
        x <<= 1;
        n++;
    }
    return n;
}

static inline int32_t g723_norm_l(int32_t a) {
    if (a == 0) {
        return 0;
    }
    int32_t x = (a < 0) ? ~a : a;
    int32_t n = 0;
    while (x < 0x40000000 && n < 31) {
        x <<= 1;
        n++;
    }
    return n;
}

static inline int16_t g723_div_s(int16_t a, int16_t b) {
    if (a == b) {
        return 32767;
    }
    int32_t num = (int32_t)a;
    int32_t den = (int32_t)b;
    int32_t q = 0;
    for (int i = 0; i < 15; i++) {
        q <<= 1;
        num <<= 1;
        if (num >= den) {
            num -= den;
            q += 1;
        }
    }
    return (int16_t)q;
}

uint32_t g723_isqrt64(uint64_t v);

#ifdef __cplusplus
}
#endif

#endif /* G723_BASICOP_H */
