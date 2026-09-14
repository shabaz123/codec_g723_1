#ifndef G723_QDEC_H
#define G723_QDEC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "codec_g723_1.h"
#include "g723_consts.h"
#include "g723_linepack.h"
#include "g723_spec_exc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t num_mem[G723_LPC_ORDER];
    int16_t den_mem[G723_LPC_ORDER];
    int16_t tilt_prev;
    int16_t tilt_k1;
    int32_t agc_gain_q12;
} g723_postfilter_state_t;

typedef struct {
    int16_t prev_lsp[G723_LPC_ORDER];
    int32_t exc_hist[G723_EXC_HIST_LEN];
    int32_t syn_mem[G723_LPC_ORDER];
    g723_postfilter_state_t pf;

    int32_t last_lag;
    int32_t last_lag2;
    int32_t last_taps_sum_q15;
    int32_t last_gain_unvoiced;
    int16_t pcm_hist[G723_ERASURE_CLASSIFIER_HISTORY_LEN];
    uint32_t erased_run;

    bool postfilter;
    bool clamp_syn_mem;
} g723_qdec_state_t;

void g723_qdec_init(g723_qdec_state_t *state);
void g723_qdec_reset(g723_qdec_state_t *state);
void g723_qdec_set_postfilter(g723_qdec_state_t *state, bool enabled);

void g723_qdec_decode_params(
    g723_qdec_state_t *state,
    const g723_frame_params_t *params,
    int16_t pcm_out[G723_FRAME_SIZE_SAMPLES]
);

void g723_qdec_decode_erased(
    g723_qdec_state_t *state,
    int16_t pcm_out[G723_FRAME_SIZE_SAMPLES]
);

g723_result_t g723_qdec_decode_frame(
    g723_qdec_state_t *state,
    const uint8_t *input,
    size_t input_size,
    int16_t pcm_out[G723_FRAME_SIZE_SAMPLES]
);

#ifdef __cplusplus
}
#endif

#endif /* G723_QDEC_H */
