#ifndef G723_ENCODER_H
#define G723_ENCODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "codec_g723_1.h"
#include "g723_consts.h"
#include "g723_linepack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define G723_WSPEECH_HIST_LEN (G723_PITCH_MAX + 3u) /* 145 */
#define G723_SHADOW_EXC_HIST_LEN (G723_PITCH_MAX + G723_SUBFRAME_SIZE) /* 202 */

typedef struct {
    float syn[G723_LPC_ORDER];
    float wz[G723_LPC_ORDER];
    float wp[G723_LPC_ORDER];
    float p_hist[G723_WSPEECH_HIST_LEN];
} g723_combined_mem_t;

typedef struct {
    g723_rate_t rate;
    bool highpass;
    int16_t hp_x_prev;
    int32_t hp_acc;

    int16_t framer_delay[G723_LOOKAHEAD_SAMPLES];
    float lpc_tail[G723_SUBFRAME_SIZE];
    float prev_unq_lsp[G723_LPC_ORDER];

    float wght_x_mem[G723_LPC_ORDER];
    float wght_f_mem[G723_LPC_ORDER];
    float wspeech_hist[G723_WSPEECH_HIST_LEN];

    g723_combined_mem_t comb_mem;

    /* Shadow decoder state */
    float shadow_prev_lsp[G723_LPC_ORDER];
    float shadow_prev_lsp_freq[G723_LPC_ORDER];
    float shadow_exc_history[G723_SHADOW_EXC_HIST_LEN];
    float shadow_syn_mem[G723_LPC_ORDER];
} g723_encoder_state_t;

void g723_encoder_init(g723_encoder_state_t *state, g723_rate_t rate);
void g723_encoder_reset(g723_encoder_state_t *state);
void g723_encoder_set_highpass(g723_encoder_state_t *state, bool enabled);
void g723_encoder_set_rate(g723_encoder_state_t *state, g723_rate_t rate);

void g723_encoder_analyse_frame(
    g723_encoder_state_t *state,
    const int16_t pcm_in[G723_FRAME_SIZE_SAMPLES],
    g723_frame_params_t *params_out
);

g723_result_t g723_encoder_encode_frame(
    g723_encoder_state_t *state,
    const int16_t pcm_in[G723_FRAME_SIZE_SAMPLES],
    uint8_t *output,
    size_t *output_size
);

#ifdef __cplusplus
}
#endif

#endif /* G723_ENCODER_H */
