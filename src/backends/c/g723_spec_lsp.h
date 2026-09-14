#ifndef G723_SPEC_LSP_H
#define G723_SPEC_LSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "g723_consts.h"

#ifdef __cplusplus
extern "C" {
#endif

void g723_split_lsp_index(uint32_t lsp_index, uint8_t bands[3]);
uint32_t g723_combine_lsp_index(const uint8_t bands[3]);

void g723_lsp_dc_freq(float freq[G723_LPC_ORDER]);
void g723_lsp_freq_to_cosines(const float freq[G723_LPC_ORDER], float cos_out[G723_LPC_ORDER]);
void g723_lsp_cosines_to_freq(const float cos_in[G723_LPC_ORDER], float freq_out[G723_LPC_ORDER]);

void g723_decode_lsp_freq(
    uint32_t lsp_index,
    const float prev[G723_LPC_ORDER],
    float out[G723_LPC_ORDER]
);

uint32_t g723_quantise_lsp_freq(
    const float p_unq[G723_LPC_ORDER],
    const float prev[G723_LPC_ORDER],
    float decoded_out[G723_LPC_ORDER]
);

/* Fixed-point routines for decoder synthesis */
void g723_lsp_decode_q15(
    uint32_t lsp_index,
    const int16_t prev[G723_LPC_ORDER],
    int16_t out[G723_LPC_ORDER]
);

void g723_lsp_extrapolate_q15(
    const int16_t prev[G723_LPC_ORDER],
    int16_t out[G723_LPC_ORDER]
);

bool g723_lsp_stability_q15(
    int16_t p[G723_LPC_ORDER],
    int16_t delta_min
);

void g723_lsp_interpolate_q15(
    size_t subframe,
    const int16_t prev[G723_LPC_ORDER],
    const int16_t cur[G723_LPC_ORDER],
    int16_t out[G723_LPC_ORDER]
);

int16_t g723_cos_q14(int16_t freq_q15);

void g723_lsp_to_lpc_q13(
    const int16_t lsp_freq[G723_LPC_ORDER],
    int16_t a_out[G723_LPC_ORDER]
);

/* Direct-form conversion */
void g723_lsp_to_lpc(
    const float lsp_cos[G723_LPC_ORDER],
    float a_out[G723_LPC_ORDER + 1]
);

bool g723_lpc_to_lsp(
    const float a[G723_LPC_ORDER + 1],
    float lsp_cos_out[G723_LPC_ORDER]
);

void g723_lsp_dc_cosines(float lsp_cos[G723_LPC_ORDER]);

bool g723_enforce_lsp_stability(
    const float lsp_cos[G723_LPC_ORDER],
    float delta_min_hz,
    float out[G723_LPC_ORDER]
);

void g723_interpolate_lsp(
    size_t subframe,
    const float prev[G723_LPC_ORDER],
    const float cur[G723_LPC_ORDER],
    float out[G723_LPC_ORDER]
);

#ifdef __cplusplus
}
#endif

#endif /* G723_SPEC_LSP_H */
