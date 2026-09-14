#ifndef G723_SPEC_EXC_H
#define G723_SPEC_EXC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "g723_consts.h"
#include "g723_linepack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define G723_EXC_HIST_LEN 146u

typedef struct {
    int16_t taps[G723_ACB_TAPS]; /* Q13 */
    int16_t fcb_gain;            /* Word16 amplitude (table << 1) */
    size_t pgindex;
    size_t mgindex;
    bool train;
} g723_gain_info_t;

static inline bool g723_uses_short_lag_gain(g723_rate_t rate, int32_t lag_base) {
    return rate == G723_RATE_HIGH && lag_base < G723_SHORT_LAG_LIMIT;
}

static inline size_t g723_gain_vq_rows(g723_rate_t rate, int32_t lag_base) {
    return g723_uses_short_lag_gain(rate, lag_base) ? G723_GAIN_ROWS_85 : G723_GAIN_ROWS_170;
}

void g723_decode_gain_word(
    g723_rate_t rate,
    int32_t lag_base,
    uint32_t gind,
    g723_gain_info_t *out
);

uint32_t g723_encode_gain_word(
    g723_rate_t rate,
    int32_t lag_base,
    size_t pgindex,
    size_t mgindex,
    bool train
);

void g723_acb_taps_q13(
    g723_rate_t rate,
    int32_t lag_base,
    size_t pgindex,
    int16_t taps[G723_ACB_TAPS]
);

int16_t g723_fcb_gain_val(size_t mgindex);

void g723_acb_contribution(
    const int32_t *hist,
    size_t hist_len,
    int32_t lag,
    const int16_t taps[G723_ACB_TAPS],
    int32_t out[G723_SUBFRAME_SIZE]
);

void g723_mpmlq_fixed_vector(
    uint32_t pos_code,
    uint32_t psig,
    uint8_t grid,
    size_t n_pulses,
    int16_t gain,
    bool train,
    int32_t lag_base,
    int32_t out[G723_SUBFRAME_SIZE]
);

void g723_acelp_fixed_vector(
    uint32_t pos,
    uint32_t psig,
    uint8_t grid,
    int16_t gain,
    int32_t out[G723_SUBFRAME_SIZE]
);

void g723_acelp_pitch_enhance(
    int32_t v[G723_SUBFRAME_SIZE],
    int32_t lag,
    size_t pgindex
);

void g723_acelp_enhanced_impulse_response(
    float *h,
    size_t h_len,
    int32_t lag,
    size_t pgindex
);

float g723_acb_row_score(
    g723_rate_t rate,
    int32_t lag_base,
    size_t pgindex,
    const float corr[G723_ACB_ROW_TERMS]
);

size_t g723_nearest_fcb_gain(float g);

#ifdef __cplusplus
}
#endif

#endif /* G723_SPEC_EXC_H */
