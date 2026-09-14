#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../g723_spec_exc.h"
#include "../g723_tables.h"

int main(void) {
    printf("Testing spec_exc module...\n");

    // 1. Gain word round trip
    for (size_t pg = 0; pg < 170; pg += 17) {
        for (size_t mg = 0; mg < 24; mg += 5) {
            uint32_t w = g723_encode_gain_word(G723_RATE_LOW, 100, pg, mg, false);
            assert(w < 4096);
            g723_gain_info_t d;
            g723_decode_gain_word(G723_RATE_LOW, 100, w, &d);
            assert(d.pgindex == pg);
            assert(d.mgindex == mg);
            assert(!d.train);

            // High rate with long lag uses same layout
            g723_gain_info_t d_high;
            g723_decode_gain_word(G723_RATE_HIGH, 58, w, &d_high);
            assert(d_high.pgindex == pg);
            assert(d_high.mgindex == mg);
            assert(!d_high.train);
        }
    }

    // Short-lag layout with train bit
    for (size_t pg = 0; pg < 85; pg += 21) {
        for (size_t mg = 0; mg < 24; mg += 7) {
            for (int tr = 0; tr <= 1; tr++) {
                uint32_t w = g723_encode_gain_word(G723_RATE_HIGH, 20, pg, mg, tr != 0);
                assert(w < 4096);
                g723_gain_info_t d;
                g723_decode_gain_word(G723_RATE_HIGH, 20, w, &d);
                assert(d.pgindex == pg);
                assert(d.mgindex == mg);
                assert(d.train == (tr != 0));
            }
        }
    }

    // 2. Taps in Q13
    int16_t taps[5] = {0};
    g723_acb_taps_q13(G723_RATE_HIGH, 20, 5, taps);
    // Row 5 of 85-entry table: -125, -40, -264, 381, 5027
    assert(taps[0] == -125);
    assert(taps[1] == -40);
    assert(taps[2] == -264);
    assert(taps[3] == 381);
    assert(taps[4] == 5027);

    // 3. nearest_fcb_gain
    for (size_t j = 0; j < 24; j++) {
        float val = (float)G723_FIXED_CODEBOOK_GAIN_Q15[j] / 16384.0f;
        assert(g723_nearest_fcb_gain(val) == j);
    }

    // 4. ACELP fixed vector
    // pos = slot0(0) | (slot1(1)<<3) | (slot2(2)<<6) | (slot3(3)<<9)
    // track0 slot0 base0 = 0
    // track1 slot1 base2 + 8 = 10
    // track2 slot2 base4 + 16 = 20
    // track3 slot3 base6 + 24 = 30
    uint32_t test_pos = 0 | (1 << 3) | (2 << 6) | (3 << 9);
    uint32_t test_psig = 0b0011; // tracks 0 and 1 positive, tracks 2 and 3 negative
    int32_t fcb_out[60] = {0};
    g723_acelp_fixed_vector(test_pos, test_psig, 0, 1000, fcb_out);
    assert(fcb_out[0] == 1000);
    assert(fcb_out[10] == 1000);
    assert(fcb_out[20] == -1000);
    assert(fcb_out[30] == -1000);
    for (int i = 0; i < 60; i++) {
        if (i != 0 && i != 10 && i != 20 && i != 30) {
            assert(fcb_out[i] == 0);
        }
    }

    // 5. ACELP pitch enhancement
    // Enhance with lag=20
    g723_acelp_pitch_enhance(fcb_out, 20, 1);
    // Pulse at 0 is enhanced at delay = lag + selector. For row 1, selector=0, gain=2489
    // delay = 20 -> sample 20 should get scaled addition
    printf("Sample 20 before was -1000, now=%d\n", fcb_out[20]);

    // 6. MP-MLQ fixed vector with train
    // 5 pulses, slots = {0, 5, 10, 15, 20}
    size_t slots5[5] = {0, 5, 10, 15, 20};
    uint32_t code5 = 0;
    assert(g723_fcbk_pack_positions(slots5, 5, &code5));
    int32_t mp_out[60] = {0};
    // train=true, lag_base=25
    g723_mpmlq_fixed_vector(code5, 0, 0, 5, 500, true, 25, mp_out);
    // base 0 -> repeat at 0, 25, 50
    assert(mp_out[0] == 500);
    assert(mp_out[25] == 500);
    assert(mp_out[50] == 500);

    // 7. ACB contribution
    int32_t hist[150] = {0};
    for (int i = 0; i < 150; i++) {
        hist[i] = 1000;
    }
    int32_t u[60] = {0};
    int16_t unity_taps[5] = {0, 0, 16384, 0, 0}; // tap 2 is delay L, unity gain Q13
    g723_acb_contribution(hist, 150, 30, unity_taps, u);
    for (int i = 0; i < 60; i++) {
        assert(u[i] == 1000);
    }

    printf("All spec_exc tests passed!\n");
    return 0;
}
