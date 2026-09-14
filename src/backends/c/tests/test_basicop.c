#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../g723_basicop.h"

int main(void) {
    printf("Testing basicop...\n");

    // add_sub_saturate_at_the_rails
    assert(g723_add(30000, 10000) == 32767);
    assert(g723_add(-30000, -10000) == -32768);
    assert(g723_add(-1, 1) == 0);
    assert(g723_sub(-30000, 10000) == -32768);
    assert(g723_sub(30000, -10000) == 32767);
    assert(g723_sub(100, 42) == 58);

    // abs_and_negate_handle_int_min
    assert(g723_abs_s(-32768) == 32767);
    assert(g723_abs_s(-5) == 5);
    assert(g723_negate(-32768) == 32767);
    assert(g723_negate(7) == -7);

    // mult_is_q15_with_single_saturating_pair
    assert(g723_mult(-32768, -32768) == 32767);
    assert(g723_mult(16384, 16384) == 8192);
    assert(g723_mult(-16384, 16384) == -8192);
    assert(g723_mult(32767, 32767) == 32766);
    assert(g723_mult_r(32767, 32767) == 32766);
    assert(g723_mult_r(3, 16384) == 2);
    assert(g723_mult(3, 16384) == 1);

    // l_mult_doubles_and_saturates
    assert(g723_l_mult(-32768, -32768) == INT32_MAX);
    assert(g723_l_mult(16384, 16384) == 0x20000000);
    assert(g723_l_mult(1, 1) == 2);
    assert(g723_l_mac(5, 1, 1) == 7);
    assert(g723_l_msu(5, 1, 1) == 3);
    assert(g723_l_mac(INT32_MAX, 100, 100) == INT32_MAX);
    assert(g723_l_msu(INT32_MIN, 100, 100) == INT32_MIN);

    // shifts_saturate_and_sign_extend
    assert(g723_shl(0x2000, 2) == 32767);
    assert(g723_shl(-0x2000, 2) == -32768);
    assert(g723_shl(3, 2) == 12);
    assert(g723_shl(3, -1) == 1);
    assert(g723_shr(-1, 4) == -1);
    assert(g723_shr(16, 2) == 4);
    assert(g723_shr(5, -2) == 20);
    assert(g723_shr(32767, 20) == 0);
    assert(g723_shr(-32768, 20) == -1);
    assert(g723_shl(1, 20) == 32767);

    assert(g723_l_shl(0x20000000, 2) == INT32_MAX);
    assert(g723_l_shl(-0x20000000, 2) == INT32_MIN);
    assert(g723_l_shl(12, 2) == 48);
    assert(g723_l_shr(-4, 1) == -2);
    assert(g723_l_shr(4, -1) == 8);
    assert(g723_l_shl(1, 40) == INT32_MAX);
    assert(g723_l_shr(-1, 40) == -1);

    // extract_round_deposit
    assert(g723_l_deposit_h(0x1234) == 0x12340000);
    assert(g723_l_deposit_l(-2) == -2);
    assert(g723_extract_h(0x12348000) == 0x1234);
    assert(g723_extract_l(0x12348000) == -32768);
    assert(g723_round16(0x12348000) == 0x1235);
    assert(g723_round16(0x12347FFF) == 0x1234);
    assert(g723_round16(0x7FFFFFFF) == 32767);

    // norms
    assert(g723_norm_s(0) == 0);
    assert(g723_norm_s(1) == 14);
    assert(g723_norm_s(0x4000) == 0);
    assert(g723_norm_s(0x3FFF) == 1);
    assert(g723_norm_s(-1) == 15);
    assert(g723_norm_s(-32768) == 0);

    const int16_t s_cases[] = {1, 2, 5, 100, 3000, 16383, 16384, 32767, -7, -32768};
    for (size_t i = 0; i < sizeof(s_cases)/sizeof(s_cases[0]); i++) {
        int16_t a = s_cases[i];
        int32_t n = g723_norm_s(a);
        int16_t v = g723_shl(a, n);
        assert((v >= 16384 && v <= 32767) || (v >= -32768 && v <= -16385));
    }

    assert(g723_norm_l(0) == 0);
    assert(g723_norm_l(1) == 30);
    assert(g723_norm_l(INT32_MIN) == 0);

    const int32_t l_cases[] = {1, 77, 0x3FFFFFFF, 0x40000000, INT32_MAX, -1, -12345};
    for (size_t i = 0; i < sizeof(l_cases)/sizeof(l_cases[0]); i++) {
        int32_t a = l_cases[i];
        int32_t n = g723_norm_l(a);
        int32_t v = g723_l_shl(a, n);
        assert(v < -0x40000000 || v >= 0x40000000);
    }

    // div_s
    assert(g723_div_s(1, 1) == 32767);
    assert(g723_div_s(0, 5) == 0);
    assert(g723_div_s(1, 2) == 16384);
    assert(g723_div_s(1, 4) == 8192);
    assert(g723_div_s(3, 4) == 24576);
    assert(g723_div_s(1, 3) == 10922);

    // isqrt64
    assert(g723_isqrt64(0) == 0);
    assert(g723_isqrt64(1) == 1);
    assert(g723_isqrt64(3) == 1);
    assert(g723_isqrt64(4) == 2);
    assert(g723_isqrt64(1ULL << 30) == (1ULL << 15));
    assert(g723_isqrt64(UINT64_MAX) == UINT32_MAX);

    const uint64_t sqrt_cases[] = {2, 99, 12345, 1ULL << 33, (1ULL << 40) - 1, 987654321987ULL};
    for (size_t i = 0; i < sizeof(sqrt_cases)/sizeof(sqrt_cases[0]); i++) {
        uint64_t v = sqrt_cases[i];
        uint64_t r = g723_isqrt64(v);
        assert(r * r <= v);
        assert((r + 1) * (r + 1) > v);
    }

    // mac chain
    int32_t acc = 0;
    for (int i = 0; i < 4096; i++) {
        acc = g723_l_mac(acc, 32767, 32767);
    }
    assert(acc == INT32_MAX);
    acc = g723_l_msu(acc, 32767, 32767);
    assert(acc < INT32_MAX);

    printf("All basicop tests passed!\n");
    return 0;
}
