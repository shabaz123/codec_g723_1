#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "g723_qdec.h"
#include "g723_basicop.h"
#include "g723_tables.h"
#include "g723_spec_lsp.h"
#include "g723_spec_exc.h"
#include "g723_linepack.h"

static void test_decode_zeroed_frames(void) {
    printf("Testing QDecoder on zeroed frames (High and Low rate)...\n");
    g723_qdec_state_t dec;
    g723_qdec_init(&dec);
    g723_qdec_set_postfilter(&dec, false);

    g723_frame_params_t p_high;
    memset(&p_high, 0, sizeof(p_high));
    p_high.rate = G723_RATE_HIGH;

    int16_t pcm[G723_FRAME_SIZE_SAMPLES];
    g723_qdec_decode_params(&dec, &p_high, pcm);

    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) {
        assert(abs(pcm[i]) < 1000);
    }

    g723_qdec_reset(&dec);
    g723_qdec_set_postfilter(&dec, false);

    g723_frame_params_t p_low;
    memset(&p_low, 0, sizeof(p_low));
    p_low.rate = G723_RATE_LOW;

    g723_qdec_decode_params(&dec, &p_low, pcm);
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) {
        assert(abs(pcm[i]) < 1000);
    }
}

static void test_erasure_attenuation(void) {
    printf("Testing QDecoder erasure concealment run...\n");
    g723_qdec_state_t dec;
    g723_qdec_init(&dec);
    g723_qdec_set_postfilter(&dec, false);

    g723_frame_params_t p;
    memset(&p, 0, sizeof(p));
    p.rate = G723_RATE_LOW;
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        p.gain[s] = 23 * 24 + 20;
        p.pos[s] = 1 | (2 << 3) | (3 << 6) | (4 << 9);
    }

    int16_t pcm[G723_FRAME_SIZE_SAMPLES];
    for (int i = 0; i < 4; i++) {
        g723_qdec_decode_params(&dec, &p, pcm);
    }

    int64_t e1 = 0, e2 = 0, e3 = 0, e4 = 0, e5 = 0;

    g723_qdec_decode_erased(&dec, pcm);
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) e1 += (int64_t)pcm[i] * pcm[i];

    g723_qdec_decode_erased(&dec, pcm);
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) e2 += (int64_t)pcm[i] * pcm[i];

    g723_qdec_decode_erased(&dec, pcm);
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) e3 += (int64_t)pcm[i] * pcm[i];

    g723_qdec_decode_erased(&dec, pcm);
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) e4 += (int64_t)pcm[i] * pcm[i];

    g723_qdec_decode_erased(&dec, pcm);
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) e5 += (int64_t)pcm[i] * pcm[i];

    assert(e1 > 0);
    assert(e2 <= e1);
    assert(e3 <= e2);
    assert(e4 < e1 / 4);
    assert(e5 <= (e4 > 0 ? e4 : 1));

    // Good frame restores signal
    g723_qdec_decode_params(&dec, &p, pcm);
    int64_t e_good = 0;
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) e_good += (int64_t)pcm[i] * pcm[i];
    assert(e_good > e5);
}

static void test_postfilter_toggle(void) {
    printf("Testing QDecoder with and without postfilter...\n");
    g723_qdec_state_t dec_pf, dec_nopf;
    g723_qdec_init(&dec_pf);
    g723_qdec_init(&dec_nopf);
    g723_qdec_set_postfilter(&dec_pf, true);
    g723_qdec_set_postfilter(&dec_nopf, false);

    g723_frame_params_t p;
    memset(&p, 0, sizeof(p));
    p.rate = G723_RATE_HIGH;
    p.lsp_index = 0x12345;
    for (int s = 0; s < 4; s++) {
        p.gain[s] = 1000;
        p.acl[s] = 30;
    }

    int16_t pcm_pf[G723_FRAME_SIZE_SAMPLES];
    int16_t pcm_nopf[G723_FRAME_SIZE_SAMPLES];

    g723_qdec_decode_params(&dec_pf, &p, pcm_pf);
    g723_qdec_decode_params(&dec_nopf, &p, pcm_nopf);

    // Both should produce valid non-trivial signals
    int64_t e_pf = 0, e_nopf = 0;
    for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) {
        e_pf += (int64_t)pcm_pf[i] * pcm_pf[i];
        e_nopf += (int64_t)pcm_nopf[i] * pcm_nopf[i];
    }
    assert(e_pf > 0);
    assert(e_nopf > 0);
}

static void test_decode_frame_bytes(void) {
    printf("Testing g723_qdec_decode_frame from byte bitstream...\n");
    g723_qdec_state_t dec;
    g723_qdec_init(&dec);

    uint8_t zero_high[G723_HIGH_RATE_BYTES] = {0}; // Rate high: hdr = 0
    int16_t pcm[G723_FRAME_SIZE_SAMPLES];
    g723_result_t res = g723_qdec_decode_frame(&dec, zero_high, sizeof(zero_high), pcm);
    assert(res == G723_RESULT_OK);

    uint8_t zero_low[G723_LOW_RATE_BYTES] = {1}; // Rate low: hdr = 1
    res = g723_qdec_decode_frame(&dec, zero_low, sizeof(zero_low), pcm);
    assert(res == G723_RESULT_OK);

    // SID frame
    uint8_t sid[G723_SID_BYTES] = {2};
    res = g723_qdec_decode_frame(&dec, sid, sizeof(sid), pcm);
    assert(res == G723_RESULT_OK);

    // Untransmitted
    uint8_t untrans[1] = {3};
    res = g723_qdec_decode_frame(&dec, untrans, sizeof(untrans), pcm);
    assert(res == G723_RESULT_OK);
}

int main(void) {
    printf("=== Starting G.723.1 Decoder Tests ===\n");
    test_decode_zeroed_frames();
    test_erasure_attenuation();
    test_postfilter_toggle();
    test_decode_frame_bytes();
    printf("=== All G.723.1 Decoder Tests Passed! ===\n");
    return 0;
}
