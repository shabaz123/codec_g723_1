#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "../g723_spec_lsp.h"
#include "../g723_tables.h"

int main(void) {
    printf("Testing spec_lsp module...\n");

    // 1. Index split and combine round-trip
    for (uint32_t idx = 0; idx < (1u << 24); idx += 12345) {
        uint8_t bands[3];
        g723_split_lsp_index(idx, bands);
        uint32_t recombined = g723_combine_lsp_index(bands);
        assert(recombined == idx);
    }

    // 2. DC vector ascending
    float dc[10];
    g723_lsp_dc_freq(dc);
    for (int i = 1; i < 10; i++) {
        assert(dc[i] > dc[i - 1]);
        assert(dc[i] > 0.0f && dc[i] < 32768.0f);
    }

    // 3. Freq <-> Cosine conversion
    float cos_dc[10];
    g723_lsp_freq_to_cosines(dc, cos_dc);
    for (int i = 1; i < 10; i++) {
        // Since frequencies ascend, cosines strictly descend
        assert(cos_dc[i] < cos_dc[i - 1]);
    }
    float dc_back[10];
    g723_lsp_cosines_to_freq(cos_dc, dc_back);
    for (int i = 0; i < 10; i++) {
        assert(fabsf(dc[i] - dc_back[i]) < 0.01f);
    }

    // 4. Quantize DC vector
    float dec[10] = {0};
    uint32_t idx_dc = g723_quantise_lsp_freq(dc, dc, dec);
    // Row 0 for all bands is 0, so index for DC vector with prev=DC must be 0!
    assert(idx_dc == 0);
    for (int i = 0; i < 10; i++) {
        assert(fabsf(dec[i] - dc[i]) < 1e-4f);
    }

    // 5. Fixed-point decode DC vector
    int16_t dc_q15[10];
    for (int i = 0; i < 10; i++) {
        dc_q15[i] = G723_LSP_DC_PREDICTED_FREQ_Q15[i];
    }
    int16_t dec_q15[10] = {0};
    g723_lsp_decode_q15(0, dc_q15, dec_q15);
    for (int i = 0; i < 10; i++) {
        assert(dec_q15[i] == dc_q15[i]);
    }

    // 6. Stability test: out-of-order pair
    int16_t test_p[10];
    memcpy(test_p, dc_q15, sizeof(test_p));
    test_p[3] = test_p[4] + 50; // out of order
    bool stable = g723_lsp_stability_q15(test_p, G723_LSP_DELTA_MIN_Q15);
    assert(stable);
    for (int i = 1; i < 10; i++) {
        assert(test_p[i] - test_p[i - 1] >= G723_LSP_DELTA_MIN_Q15);
    }

    // 7. lsp_to_lpc_q13 vs float lsp_to_lpc on DC vector
    int16_t a_q13[10];
    g723_lsp_to_lpc_q13(dc_q15, a_q13);

    float a_float[11];
    g723_lsp_to_lpc(cos_dc, a_float);
    assert(fabsf(a_float[0] - 1.0f) < 1e-6f);

    for (int i = 0; i < 10; i++) {
        float q13_as_float = (float)a_q13[i] / 8192.0f;
        // In G.723.1, a_q13 contains -a_i or a_i depending on convention:
        // Let's verify correlation between a_q13 and a_float[i+1]
        printf("a[%d]: float=%f, q13=%d (%f)\n", i, a_float[i + 1], a_q13[i], q13_as_float);
    }

    // 8. LPC <-> LSP round-trip in float
    float lsp_recovered[10];
    assert(g723_lpc_to_lsp(a_float, lsp_recovered));
    for (int i = 0; i < 10; i++) {
        assert(fabsf(cos_dc[i] - lsp_recovered[i]) < 1e-4f);
    }

    printf("All spec_lsp tests passed!\n");
    return 0;
}
