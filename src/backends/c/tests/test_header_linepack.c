#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../g723_header.h"
#include "../g723_linepack.h"

// Simple LCG for deterministic pseudo-random tests
static uint32_t lcg_next(uint64_t *state) {
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(*state >> 33);
}

static uint32_t lcg_below(uint64_t *state, uint32_t n) {
    return lcg_next(state) % n;
}

int main(void) {
    printf("Testing header and linepack...\n");

    // Test header
    uint8_t b0 = 0x00;
    assert(g723_parse_frame_type(&b0, 1) == G723_FRAME_TYPE_6300);
    assert(g723_expected_frame_size(G723_FRAME_TYPE_6300) == 24);

    uint8_t b1 = 0x01;
    assert(g723_parse_frame_type(&b1, 1) == G723_FRAME_TYPE_5300);
    assert(g723_expected_frame_size(G723_FRAME_TYPE_5300) == 20);

    uint8_t b2 = 0x02;
    assert(g723_parse_frame_type(&b2, 1) == G723_FRAME_TYPE_SID);
    assert(g723_expected_frame_size(G723_FRAME_TYPE_SID) == 4);

    uint8_t b3 = 0x03;
    assert(g723_parse_frame_type(&b3, 1) == G723_FRAME_TYPE_UNTRANSMITTED);
    assert(g723_expected_frame_size(G723_FRAME_TYPE_UNTRANSMITTED) == 1);

    // Test msbpos_combine and split over entire 8100 mixed-radix domain
    for (uint32_t d0 = 0; d0 < 10; d0++) {
        for (uint32_t d1 = 0; d1 < 9; d1++) {
            for (uint32_t d2 = 0; d2 < 10; d2++) {
                for (uint32_t d3 = 0; d3 < 9; d3++) {
                    uint32_t pos[4] = {
                        d0 << 16,
                        d1 << 14,
                        d2 << 16,
                        d3 << 14
                    };
                    uint32_t word = g723_msbpos_combine(pos);
                    assert(word < 8100);
                    uint32_t digits[4] = {0};
                    assert(g723_msbpos_split(word, digits));
                    assert(digits[0] == d0);
                    assert(digits[1] == d1);
                    assert(digits[2] == d2);
                    assert(digits[3] == d3);
                }
            }
        }
    }
    uint32_t dummy_digits[4];
    assert(!g723_msbpos_split(8100, dummy_digits));
    assert(!g723_msbpos_split(9999, dummy_digits));

    // Test pack/unpack round-trip on zeroed frames
    g723_frame_params_t p_high_zero = {0};
    p_high_zero.rate = G723_RATE_HIGH;
    uint8_t buf[32] = {0};
    size_t out_sz = 0;
    assert(g723_pack_frame(&p_high_zero, buf, sizeof(buf), &out_sz) == G723_RESULT_OK);
    assert(out_sz == 24);
    assert((buf[0] & 0x03) == 0x00);

    g723_frame_params_t unpacked_high = {0};
    assert(g723_unpack_frame(buf, out_sz, &unpacked_high) == G723_RESULT_OK);
    assert(unpacked_high.rate == G723_RATE_HIGH);
    assert(unpacked_high.lsp_index == 0);
    for (int s = 0; s < 4; s++) {
        assert(unpacked_high.acl[s] == 0);
        assert(unpacked_high.gain[s] == 0);
        assert(unpacked_high.grid[s] == 0);
        assert(unpacked_high.pos[s] == 0);
        assert(unpacked_high.psig[s] == 0);
    }

    g723_frame_params_t p_low_zero = {0};
    p_low_zero.rate = G723_RATE_LOW;
    assert(g723_pack_frame(&p_low_zero, buf, sizeof(buf), &out_sz) == G723_RESULT_OK);
    assert(out_sz == 20);
    assert((buf[0] & 0x03) == 0x01);

    g723_frame_params_t unpacked_low = {0};
    assert(g723_unpack_frame(buf, out_sz, &unpacked_low) == G723_RESULT_OK);
    assert(unpacked_low.rate == G723_RATE_LOW);
    assert(unpacked_low.lsp_index == 0);
    for (int s = 0; s < 4; s++) {
        assert(unpacked_low.acl[s] == 0);
        assert(unpacked_low.gain[s] == 0);
        assert(unpacked_low.grid[s] == 0);
        assert(unpacked_low.pos[s] == 0);
        assert(unpacked_low.psig[s] == 0);
    }

    // Randomized round-trip test (500 iterations each rate)
    uint64_t rng = 123456789ULL;
    const uint32_t acl_bits[4] = {7, 2, 7, 2};
    const uint32_t max_pos[4] = {593775, 142506, 593775, 142506};
    const uint32_t high_psig_bits[4] = {6, 5, 6, 5};

    for (int iter = 0; iter < 500; iter++) {
        g723_frame_params_t p = {0};
        p.rate = G723_RATE_HIGH;
        p.lsp_index = lcg_below(&rng, 1 << 24);
        for (int s = 0; s < 4; s++) {
            p.acl[s] = lcg_below(&rng, 1 << acl_bits[s]);
            p.gain[s] = lcg_below(&rng, 1 << 12);
            p.grid[s] = (uint8_t)lcg_below(&rng, 2);
            p.pos[s] = lcg_below(&rng, max_pos[s]);
            p.psig[s] = lcg_below(&rng, 1 << high_psig_bits[s]);
        }
        assert(g723_pack_frame(&p, buf, sizeof(buf), &out_sz) == G723_RESULT_OK);
        assert(out_sz == 24);

        g723_frame_params_t up = {0};
        assert(g723_unpack_frame(buf, out_sz, &up) == G723_RESULT_OK);
        assert(up.rate == p.rate);
        assert(up.lsp_index == p.lsp_index);
        for (int s = 0; s < 4; s++) {
            assert(up.acl[s] == p.acl[s]);
            assert(up.gain[s] == p.gain[s]);
            assert(up.grid[s] == p.grid[s]);
            assert(up.pos[s] == p.pos[s]);
            assert(up.psig[s] == p.psig[s]);
        }
    }

    for (int iter = 0; iter < 500; iter++) {
        g723_frame_params_t p = {0};
        p.rate = G723_RATE_LOW;
        p.lsp_index = lcg_below(&rng, 1 << 24);
        for (int s = 0; s < 4; s++) {
            p.acl[s] = lcg_below(&rng, 1 << acl_bits[s]);
            p.gain[s] = lcg_below(&rng, 1 << 12);
            p.grid[s] = (uint8_t)lcg_below(&rng, 2);
            p.pos[s] = lcg_below(&rng, 1 << 12);
            p.psig[s] = lcg_below(&rng, 1 << 4);
        }
        assert(g723_pack_frame(&p, buf, sizeof(buf), &out_sz) == G723_RESULT_OK);
        assert(out_sz == 20);

        g723_frame_params_t up = {0};
        assert(g723_unpack_frame(buf, out_sz, &up) == G723_RESULT_OK);
        assert(up.rate == p.rate);
        assert(up.lsp_index == p.lsp_index);
        for (int s = 0; s < 4; s++) {
            assert(up.acl[s] == p.acl[s]);
            assert(up.gain[s] == p.gain[s]);
            assert(up.grid[s] == p.grid[s]);
            assert(up.pos[s] == p.pos[s]);
            assert(up.psig[s] == p.psig[s]);
        }
    }

    printf("All header and linepack tests passed!\n");
    return 0;
}
