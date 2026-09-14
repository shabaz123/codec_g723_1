#include "g723_spec_exc.h"
#include "g723_tables.h"
#include "g723_basicop.h"
#include <string.h>
#include <math.h>

static inline int32_t sat32(int64_t x) {
    if (x > INT32_MAX) {
        return INT32_MAX;
    } else if (x < INT32_MIN) {
        return INT32_MIN;
    } else {
        return (int32_t)x;
    }
}

void g723_acb_taps_q13(
    g723_rate_t rate,
    int32_t lag_base,
    size_t pgindex,
    int16_t taps[G723_ACB_TAPS]
) {
    bool short_lag = g723_uses_short_lag_gain(rate, lag_base);
    const int16_t *table = short_lag ? G723_ADAPTIVE_CODEBOOK_GAIN_5P3 : G723_ADAPTIVE_CODEBOOK_GAIN_6P3;
    size_t max_rows = short_lag ? G723_GAIN_ROWS_85 : G723_GAIN_ROWS_170;
    size_t row_idx = (pgindex < max_rows ? pgindex : max_rows - 1) * G723_ACB_ROW_TERMS;
    for (size_t i = 0; i < G723_ACB_TAPS; i++) {
        taps[i] = table[row_idx + i];
    }
}

int16_t g723_fcb_gain_val(size_t mgindex) {
    size_t idx = mgindex < G723_GAIN_TABLE_SIZE ? mgindex : G723_GAIN_TABLE_SIZE - 1;
    return (int16_t)(G723_FIXED_CODEBOOK_GAIN_Q15[idx] << 1);
}

void g723_decode_gain_word(
    g723_rate_t rate,
    int32_t lag_base,
    uint32_t gind,
    g723_gain_info_t *out
) {
    if (out == NULL) {
        return;
    }
    bool short_lag = g723_uses_short_lag_gain(rate, lag_base);
    size_t pg, mg;
    bool train;

    if (short_lag) {
        uint32_t masked = gind & 0x7FFu;
        pg = (size_t)(masked / G723_GAIN_TABLE_SIZE);
        if (pg >= G723_GAIN_ROWS_85) {
            pg = G723_GAIN_ROWS_85 - 1;
        }
        mg = (size_t)(masked % G723_GAIN_TABLE_SIZE);
        train = (gind & 0x800u) != 0;
    } else {
        pg = (size_t)(gind / G723_GAIN_TABLE_SIZE);
        if (pg >= G723_GAIN_ROWS_170) {
            pg = G723_GAIN_ROWS_170 - 1;
        }
        mg = (size_t)(gind % G723_GAIN_TABLE_SIZE);
        train = false;
    }

    out->pgindex = pg;
    out->mgindex = mg;
    out->train = train;
    out->fcb_gain = g723_fcb_gain_val(mg);
    g723_acb_taps_q13(rate, lag_base, pg, out->taps);
}

uint32_t g723_encode_gain_word(
    g723_rate_t rate,
    int32_t lag_base,
    size_t pgindex,
    size_t mgindex,
    bool train
) {
    bool short_lag = g723_uses_short_lag_gain(rate, lag_base);
    size_t max_pg = short_lag ? G723_GAIN_ROWS_85 : G723_GAIN_ROWS_170;
    if (pgindex >= max_pg) pgindex = max_pg - 1;
    if (mgindex >= G723_GAIN_TABLE_SIZE) mgindex = G723_GAIN_TABLE_SIZE - 1;

    uint32_t base = (uint32_t)(pgindex * G723_GAIN_TABLE_SIZE + mgindex);
    if (short_lag && train) {
        base |= 0x800u;
    }
    return base;
}

void g723_acb_contribution(
    const int32_t *hist,
    size_t hist_len,
    int32_t lag,
    const int16_t taps[G723_ACB_TAPS],
    int32_t out[G723_SUBFRAME_SIZE]
) {
    int32_t l = lag;
    if (l < (int32_t)G723_PITCH_MIN) l = (int32_t)G723_PITCH_MIN;
    if (l > (int32_t)G723_PITCH_MAX) l = (int32_t)G723_PITCH_MAX;

    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        int64_t acc = 0;
        for (size_t j = 0; j < G723_ACB_TAPS; j++) {
            size_t m = n + j;
            size_t off;
            if (m < 2) {
                off = (size_t)(l + 2 - (int32_t)m);
            } else {
                off = (size_t)(l - ((int32_t)(m - 2) % l));
            }
            int32_t samp = (off <= hist_len) ? hist[hist_len - off] : 0;
            acc += (int64_t)taps[j] * (int64_t)samp;
        }
        /* Tap shift = 14 with rounding */
        out[n] = sat32((acc + ((int64_t)1 << 13)) >> 14);
    }
}

void g723_mpmlq_fixed_vector(
    uint32_t pos_code,
    uint32_t psig,
    uint8_t grid,
    size_t n_pulses,
    int16_t gain,
    bool train,
    int32_t lag_base,
    int32_t out[G723_SUBFRAME_SIZE]
) {
    memset(out, 0, sizeof(int32_t) * G723_SUBFRAME_SIZE);

    size_t slots[6] = {0};
    if (!g723_fcbk_unpk_positions(pos_code, n_pulses, slots)) {
        return;
    }

    size_t period = (lag_base > 1) ? (size_t)lag_base : 1;
    for (size_t k = 0; k < n_pulses; k++) {
        size_t slot = slots[k];
        uint32_t bit = (uint32_t)(n_pulses - 1 - k);
        int32_t amp = ((psig >> bit) & 1u) ? -(int32_t)gain : (int32_t)gain;
        size_t base = 2 * slot + (grid & 1u);

        if (train) {
            size_t pos = base;
            while (pos < G723_SUBFRAME_SIZE) {
                out[pos] = sat32((int64_t)out[pos] + amp);
                pos += period;
            }
        } else if (base < G723_SUBFRAME_SIZE) {
            out[base] = sat32((int64_t)out[base] + amp);
        }
    }
}

void g723_acelp_fixed_vector(
    uint32_t pos,
    uint32_t psig,
    uint8_t grid,
    int16_t gain,
    int32_t out[G723_SUBFRAME_SIZE]
) {
    memset(out, 0, sizeof(int32_t) * G723_SUBFRAME_SIZE);

    for (size_t track = 0; track < 4; track++) {
        size_t slot = (size_t)((pos >> (3 * track)) & 0x7u);
        size_t sample = 0;
        if (!g723_acelp_track_position(track, slot, (grid & 1u) != 0, &sample)) {
            continue;
        }
        int32_t amp = ((psig >> track) & 1u) ? (int32_t)gain : -(int32_t)gain;
        out[sample] = sat32((int64_t)out[sample] + amp);
    }
}

void g723_acelp_pitch_enhance(
    int32_t v[G723_SUBFRAME_SIZE],
    int32_t lag,
    size_t pgindex
) {
    if (lag >= (int32_t)G723_SUBFRAME_SIZE) {
        return;
    }
    g723_pitch_1tap_ltp_t ltp;
    if (!g723_get_pitch_1tap_ltp(pgindex, &ltp) || ltp.gain == 0) {
        return;
    }
    int32_t delay = lag + (int32_t)ltp.selector;
    if (delay <= 0) {
        return;
    }
    for (size_t n = (size_t)delay; n < G723_SUBFRAME_SIZE; n++) {
        int32_t scaled = sat32(((int64_t)ltp.gain * (int64_t)v[n - (size_t)delay]) >> 15);
        v[n] = sat32((int64_t)v[n] + scaled);
    }
}

void g723_acelp_enhanced_impulse_response(
    float *h,
    size_t h_len,
    int32_t lag,
    size_t pgindex
) {
    if (lag >= (int32_t)G723_SUBFRAME_SIZE) {
        return;
    }
    g723_pitch_1tap_ltp_t ltp;
    if (!g723_get_pitch_1tap_ltp(pgindex, &ltp) || ltp.gain == 0) {
        return;
    }
    int32_t delay = lag + (int32_t)ltp.selector;
    if (delay <= 0) {
        return;
    }
    float beta = (float)ltp.gain / 32768.0f;
    for (size_t n = (size_t)delay; n < h_len; n++) {
        h[n] += beta * h[n - (size_t)delay];
    }
}

float g723_acb_row_score(
    g723_rate_t rate,
    int32_t lag_base,
    size_t pgindex,
    const float corr[G723_ACB_ROW_TERMS]
) {
    bool short_lag = g723_uses_short_lag_gain(rate, lag_base);
    const int16_t *table = short_lag ? G723_ADAPTIVE_CODEBOOK_GAIN_5P3 : G723_ADAPTIVE_CODEBOOK_GAIN_6P3;
    size_t max_rows = short_lag ? G723_GAIN_ROWS_85 : G723_GAIN_ROWS_170;
    size_t row_idx = (pgindex < max_rows ? pgindex : max_rows - 1) * G723_ACB_ROW_TERMS;

    float score = 0.0f;
    for (size_t i = 0; i < G723_ACB_ROW_TERMS; i++) {
        score += (float)table[row_idx + i] * corr[i];
    }
    return score;
}

size_t g723_nearest_fcb_gain(float g) {
    float target = fabsf(g) * 16384.0f;
    size_t best = 0;
    float best_d = 1e30f;
    for (size_t j = 0; j < G723_GAIN_TABLE_SIZE; j++) {
        float d = fabsf(target - (float)G723_FIXED_CODEBOOK_GAIN_Q15[j]);
        if (d < best_d) {
            best_d = d;
            best = j;
        }
    }
    return best;
}
