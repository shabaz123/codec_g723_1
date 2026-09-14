#include "g723_encoder.h"
#include "g723_basicop.h"
#include "g723_tables.h"
#include "g723_spec_lsp.h"
#include "g723_spec_exc.h"
#include "g723_linepack.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define G723_HP_POLE_Q15 32512
#define G723_ACELP_LOOP4_FRAME_BUDGET 600u

static inline int16_t hp_step(int16_t *x_prev, int32_t *acc, int16_t x) {
    int64_t d = (int64_t)x - (int64_t)*x_prev;
    *x_prev = x;
    int64_t pole = ((int64_t)*acc * (int64_t)G723_HP_POLE_Q15 + (1LL << 14)) >> 15;
    int64_t next = pole + (d << 15);
    if (next > INT32_MAX) {
        next = INT32_MAX;
    } else if (next < INT32_MIN) {
        next = INT32_MIN;
    }
    *acc = (int32_t)next;
    return g723_round16((int32_t)next);
}

void g723_encoder_init(g723_encoder_state_t *state, g723_rate_t rate) {
    if (state == NULL) return;
    memset(state, 0, sizeof(g723_encoder_state_t));
    state->rate = rate;
    state->highpass = true;
    g723_lsp_dc_cosines(state->prev_unq_lsp);
    g723_lsp_dc_cosines(state->shadow_prev_lsp);
    g723_lsp_dc_freq(state->shadow_prev_lsp_freq);
}

void g723_encoder_reset(g723_encoder_state_t *state) {
    if (state == NULL) return;
    g723_rate_t r = state->rate;
    bool hp = state->highpass;
    g723_encoder_init(state, r);
    state->highpass = hp;
}

void g723_encoder_set_highpass(g723_encoder_state_t *state, bool enabled) {
    if (state != NULL) {
        state->highpass = enabled;
    }
}

void g723_encoder_set_rate(g723_encoder_state_t *state, g723_rate_t rate) {
    if (state != NULL) {
        state->rate = rate;
    }
}

static void lpc_analysis(const float window[G723_LPC_WINDOW], float a_out[G723_LPC_ORDER + 1]) {
    float windowed[G723_LPC_WINDOW];
    for (size_t i = 0; i < G723_LPC_WINDOW; i++) {
        float w = (float)G723_LPC_HAMMING_WINDOW_Q15[i] / 32768.0f;
        windowed[i] = window[i] * w;
    }

    double r[G723_LPC_ORDER + 1];
    for (size_t k = 0; k <= G723_LPC_ORDER; k++) {
        double acc = 0.0;
        for (size_t i = k; i < G723_LPC_WINDOW; i++) {
            acc += (double)windowed[i] * (double)windowed[i - k];
        }
        r[k] = acc;
    }

    r[0] *= 1025.0 / 1024.0;
    for (size_t k = 1; k <= G723_LPC_ORDER; k++) {
        r[k] *= (double)G723_LPC_BINOMIAL_LAG_WINDOW_Q15[k - 1] / 32768.0;
    }

    double a[G723_LPC_ORDER + 1] = {1.0};
    double a_prev[G723_LPC_ORDER + 1] = {1.0};
    double e = r[0];

    if (e > 0.0) {
        for (size_t i = 1; i <= G723_LPC_ORDER; i++) {
            double acc = r[i];
            for (size_t j = 1; j < i; j++) {
                acc += a_prev[j] * r[i - j];
            }
            double k = -acc / e;
            if (!isfinite(k) || fabs(k) >= 1.0) {
                break;
            }
            a[i] = k;
            for (size_t j = 1; j < i; j++) {
                a[j] = a_prev[j] + k * a_prev[i - j];
            }
            e *= (1.0 - k * k);
            memcpy(a_prev, a, sizeof(double) * (G723_LPC_ORDER + 1));
            if (e <= 0.0) {
                break;
            }
        }
    }

    for (size_t i = 0; i <= G723_LPC_ORDER; i++) {
        a_out[i] = (float)a_prev[i];
    }
}

static void weighting_taps(
    const float a[G723_LPC_ORDER + 1],
    float wz[G723_LPC_ORDER],
    float wp[G723_LPC_ORDER]
) {
    for (size_t k = 0; k < G723_LPC_ORDER; k++) {
        wz[k] = a[k + 1] * ((float)G723_PERCEPTUAL_ZERO_Q15[k] / 32768.0f);
        wp[k] = a[k + 1] * ((float)G723_PERCEPTUAL_POLE_Q15[k] / 32768.0f);
    }
}

static void weight_subframe(
    const float x[G723_SUBFRAME_SIZE],
    const float wz[G723_LPC_ORDER],
    const float wp[G723_LPC_ORDER],
    float x_mem[G723_LPC_ORDER],
    float f_mem[G723_LPC_ORDER],
    float out[G723_SUBFRAME_SIZE]
) {
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        float acc = x[n];
        for (size_t k = 0; k < G723_LPC_ORDER; k++) {
            acc += wz[k] * x_mem[k] - wp[k] * f_mem[k];
        }
        for (int k = (int)G723_LPC_ORDER - 1; k > 0; k--) {
            x_mem[k] = x_mem[k - 1];
            f_mem[k] = f_mem[k - 1];
        }
        x_mem[0] = x[n];
        f_mem[0] = acc;
        out[n] = acc;
    }
}

static int32_t open_loop_pitch(const float wbuf[G723_WSPEECH_HIST_LEN + G723_FRAME_SIZE_SAMPLES], size_t half) {
    const double DB_1_25 = 1.333521432163324;
    const size_t HALF_LEN = G723_FRAME_SIZE_SAMPLES / 2;
    size_t o = G723_WSPEECH_HIST_LEN + half * HALF_LEN;
    int32_t best_lag = (int32_t)G723_PITCH_MIN;
    double best_col = -1e30;

    for (size_t j = G723_PITCH_MIN; j <= G723_PITCH_MAX; j++) {
        double num = 0.0, den = 0.0;
        for (size_t n = 0; n < HALF_LEN; n++) {
            double fj = (double)wbuf[o + n - j];
            num += (double)wbuf[o + n] * fj;
            den += fj * fj;
        }
        if (num <= 0.0 || den <= 0.0) {
            continue;
        }
        double col = (num * num) / den;
        bool take = false;
        if (best_col < -1e20) {
            take = true;
        } else if ((int32_t)j - best_lag < (int32_t)G723_PITCH_MIN) {
            take = (col > best_col);
        } else {
            take = (col > best_col * DB_1_25);
        }
        if (take) {
            best_col = col;
            best_lag = (int32_t)j;
        }
    }
    return best_lag;
}

typedef struct {
    int32_t lag;
    float beta;
} hns_params_t;

static hns_params_t harmonic_noise_shaping(
    const float wbuf[G723_WSPEECH_HIST_LEN + G723_FRAME_SIZE_SAMPLES],
    size_t off,
    int32_t l_ol
) {
    int32_t lo = l_ol - 3;
    if (lo < (int32_t)G723_PITCH_MIN) lo = (int32_t)G723_PITCH_MIN;
    int32_t hi = l_ol + 3;
    if (hi > (int32_t)G723_WSPEECH_HIST_LEN) hi = (int32_t)G723_WSPEECH_HIST_LEN;

    double cl = -1e30;
    int32_t best_lag = 0;
    double gopt = 0.0;

    for (int32_t j = lo; j <= hi; j++) {
        double num = 0.0, den = 0.0;
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            double fj = (double)wbuf[off + n - (size_t)j];
            num += (double)wbuf[off + n] * fj;
            den += fj * fj;
        }
        if (num <= 0.0 || den <= 0.0) continue;
        double cpw = (num * num) / den;
        if (cpw > cl) {
            cl = cpw;
            best_lag = j;
            gopt = num / den;
        }
    }

    hns_params_t res = {(int32_t)G723_PITCH_MIN, 0.0f};
    if (best_lag == 0) return res;

    double e = 0.0;
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        double v = (double)wbuf[off + n];
        e += v * v;
    }
    if (e <= 0.0) return res;

    double ratio = cl / e;
    if (ratio > 1.0) ratio = 1.0;
    // Harmonic noise shaping threshold: -10 * log10(1 - ratio) >= 2.0 dB
    // 1 - ratio <= 10^(-0.2) ~= 0.63095734448  <=>  ratio >= 1 - 10^(-0.2) ~= 0.36904265551980675
    if (ratio >= 0.36904265551980675) {
        if (gopt < 0.0) gopt = 0.0;
        if (gopt > 1.0) gopt = 1.0;
        res.lag = best_lag;
        res.beta = (float)(0.3125 * gopt);
    }
    return res;
}

static void run_combined(
    g723_combined_mem_t *mem,
    const float input[G723_SUBFRAME_SIZE],
    const float aq[G723_LPC_ORDER + 1],
    const float wz[G723_LPC_ORDER],
    const float wp[G723_LPC_ORDER],
    hns_params_t hns,
    float out[G723_SUBFRAME_SIZE]
) {
    float p_in[G723_SUBFRAME_SIZE];
    size_t l = (size_t)hns.lag;

    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        float s = input[n];
        for (size_t k = 0; k < G723_LPC_ORDER; k++) {
            s -= aq[k + 1] * mem->syn[k];
        }
        for (int k = (int)G723_LPC_ORDER - 1; k > 0; k--) {
            mem->syn[k] = mem->syn[k - 1];
        }
        mem->syn[0] = s;

        float f = s;
        for (size_t k = 0; k < G723_LPC_ORDER; k++) {
            f += wz[k] * mem->wz[k] - wp[k] * mem->wp[k];
        }
        for (int k = (int)G723_LPC_ORDER - 1; k > 0; k--) {
            mem->wz[k] = mem->wz[k - 1];
            mem->wp[k] = mem->wp[k - 1];
        }
        mem->wz[0] = s;
        mem->wp[0] = f;

        float past = (n >= l) ? p_in[n - l] : mem->p_hist[G723_WSPEECH_HIST_LEN - (l - n)];
        p_in[n] = f;
        out[n] = f - hns.beta * past;
    }

    memmove(&mem->p_hist[0], &mem->p_hist[G723_SUBFRAME_SIZE],
            sizeof(float) * (G723_WSPEECH_HIST_LEN - G723_SUBFRAME_SIZE));
    memcpy(&mem->p_hist[G723_WSPEECH_HIST_LEN - G723_SUBFRAME_SIZE], p_in,
           sizeof(float) * G723_SUBFRAME_SIZE);
}

static void combined_impulse_response(
    const float aq[G723_LPC_ORDER + 1],
    const float wz[G723_LPC_ORDER],
    const float wp[G723_LPC_ORDER],
    hns_params_t hns,
    float h_out[G723_SUBFRAME_SIZE]
) {
    g723_combined_mem_t zero_mem;
    memset(&zero_mem, 0, sizeof(zero_mem));
    float delta[G723_SUBFRAME_SIZE] = {0};
    delta[0] = 1.0f;
    run_combined(&zero_mem, delta, aq, wz, wp, hns, h_out);
}

static void conv_causal(
    const float x[G723_SUBFRAME_SIZE],
    const float *h,
    size_t h_len,
    float y[G723_SUBFRAME_SIZE]
) {
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        float acc = 0.0f;
        for (size_t k = 0; k <= n; k++) {
            if (k < h_len) {
                acc += x[n - k] * h[k];
            }
        }
        y[n] = acc;
    }
}

static void acb_basis_float(
    const float *hist,
    size_t hist_len,
    int32_t lag,
    float basis[G723_ACB_TAPS][G723_SUBFRAME_SIZE]
) {
    int32_t l = lag;
    if (l < (int32_t)G723_PITCH_MIN) l = (int32_t)G723_PITCH_MIN;
    if (l > (int32_t)G723_PITCH_MAX) l = (int32_t)G723_PITCH_MAX;
    size_t lu = (size_t)l;

    for (size_t j = 0; j < G723_ACB_TAPS; j++) {
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            size_t m = n + j;
            size_t off = (m < 2) ? (lu + 2 - m) : (lu - ((m - 2) % lu));
            basis[j][n] = hist[hist_len - off];
        }
    }
}

static void acb_taps_f32(g723_rate_t rate, int32_t lag_base, size_t pgindex, float taps[G723_ACB_TAPS]) {
    int16_t t_q13[G723_ACB_TAPS];
    g723_acb_taps_q13(rate, lag_base, pgindex, t_q13);
    for (size_t j = 0; j < G723_ACB_TAPS; j++) {
        taps[j] = (float)t_q13[j] / 16384.0f;
    }
}

static void acb_contribution_f32(
    const float *hist,
    size_t hist_len,
    int32_t lag,
    const float taps[G723_ACB_TAPS],
    float out[G723_SUBFRAME_SIZE]
) {
    float basis[G723_ACB_TAPS][G723_SUBFRAME_SIZE];
    acb_basis_float(hist, hist_len, lag, basis);
    memset(out, 0, sizeof(float) * G723_SUBFRAME_SIZE);
    for (size_t j = 0; j < G723_ACB_TAPS; j++) {
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            out[n] += taps[j] * basis[j][n];
        }
    }
}

static void mpmlq_fixed_vector_f32(
    uint32_t pos_code,
    uint32_t psig,
    uint8_t grid,
    size_t n_pulses,
    float gain,
    bool train,
    int32_t lag_base,
    float out[G723_SUBFRAME_SIZE]
) {
    memset(out, 0, sizeof(float) * G723_SUBFRAME_SIZE);
    size_t slots[6];
    if (!g723_fcbk_unpk_positions(pos_code, n_pulses, slots)) return;
    size_t period = (lag_base > 1) ? (size_t)lag_base : 1;
    for (size_t k = 0; k < n_pulses; k++) {
        size_t bit = n_pulses - 1 - k;
        float sign = ((psig >> bit) & 1) ? -1.0f : 1.0f;
        float amp = sign * gain;
        size_t base = 2 * slots[k] + (size_t)grid;
        if (train) {
            size_t pos = base;
            while (pos < G723_SUBFRAME_SIZE) {
                out[pos] += amp;
                pos += period;
            }
        } else if (base < G723_SUBFRAME_SIZE) {
            out[base] += amp;
        }
    }
}

static void acelp_fixed_vector_f32(
    uint32_t pos,
    uint32_t psig,
    uint8_t grid,
    float gain,
    float out[G723_SUBFRAME_SIZE]
) {
    memset(out, 0, sizeof(float) * G723_SUBFRAME_SIZE);
    for (size_t track = 0; track < 4; track++) {
        size_t slot = (pos >> (3 * track)) & 7;
        size_t sample;
        if (!g723_acelp_track_position(track, slot, grid != 0, &sample)) continue;
        float sign = ((psig >> track) & 1) ? 1.0f : -1.0f;
        out[sample] += sign * gain;
    }
}

static void acelp_pitch_enhance_f32(
    float v[G723_SUBFRAME_SIZE],
    int32_t lag,
    size_t pgindex
) {
    if (lag >= (int32_t)G723_SUBFRAME_SIZE) return;
    g723_pitch_1tap_ltp_t ltp;
    if (!g723_get_pitch_1tap_ltp(pgindex, &ltp) || ltp.gain == 0) return;
    float beta = (float)ltp.gain / 32768.0f;
    int32_t delay = lag + (int32_t)ltp.selector;
    if (delay <= 0) return;
    for (size_t n = (size_t)delay; n < G723_SUBFRAME_SIZE; n++) {
        v[n] += beta * v[n - (size_t)delay];
    }
}

static void place_pulses(
    const uint32_t positions[4],
    const int32_t signs[4],
    uint8_t grid,
    float out[G723_SUBFRAME_SIZE]
) {
    memset(out, 0, sizeof(float) * G723_SUBFRAME_SIZE);
    for (size_t track = 0; track < 4; track++) {
        size_t pos;
        if (g723_acelp_track_position(track, positions[track], grid != 0, &pos)) {
            out[pos] = (float)signs[track];
        }
    }
}

static void mpmlq_spec_search(
    const float target[G723_SUBFRAME_SIZE],
    const float h[G723_SUBFRAME_SIZE],
    size_t n_pulses,
    int32_t lag_base,
    uint32_t *pos_code_out,
    uint32_t *psig_out,
    uint8_t *grid_out,
    size_t *mg_out,
    bool *train_out
) {
    float h_energy = 0.0f;
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        h_energy += h[n] * h[n];
    }
    float d_max = 0.0f;
    for (size_t j = 0; j < G723_SUBFRAME_SIZE; j++) {
        float dj = 0.0f;
        for (size_t n = j; n < G723_SUBFRAME_SIZE; n++) {
            dj += target[n] * h[n - j];
        }
        if (fabsf(dj) > d_max) {
            d_max = fabsf(dj);
        }
    }
    float gmax = (h_energy > 0.0f) ? (d_max / h_energy) : 0.0f;
    size_t j0 = g723_nearest_fcb_gain(gmax);

    bool allow_train = (lag_base < (int32_t)G723_SHORT_LAG_LIMIT);
    float h_train[G723_SUBFRAME_SIZE];
    memcpy(h_train, h, sizeof(float) * G723_SUBFRAME_SIZE);
    if (allow_train) {
        size_t p = (lag_base > 1) ? (size_t)lag_base : 1;
        for (size_t n = p; n < G723_SUBFRAME_SIZE; n++) {
            h_train[n] = h[n] + h_train[n - p];
        }
    }

    float best_err = 1e30f;
    uint32_t best_pos_code = 0;
    uint32_t best_psig = 0;
    uint8_t best_grid = 0;
    size_t best_mg = j0;
    bool best_train = false;

    size_t n_train_opts = allow_train ? 2 : 1;
    bool train_opts[2] = {false, true};

    for (size_t t_idx = 0; t_idx < n_train_opts; t_idx++) {
        bool train = train_opts[t_idx];
        const float *hh = train ? h_train : h;
        for (uint8_t grid = 0; grid < 2; grid++) {
            size_t mg_start = (j0 >= 3) ? (j0 - 3) : 0;
            size_t mg_end = (j0 + 1 <= 23) ? (j0 + 1) : 23;
            for (size_t mg = mg_start; mg <= mg_end; mg++) {
                float g = (float)G723_FIXED_CODEBOOK_GAIN_Q15[mg] / 16384.0f;
                float res[G723_SUBFRAME_SIZE];
                memcpy(res, target, sizeof(float) * G723_SUBFRAME_SIZE);
                bool used[30] = {false};
                size_t chosen_slot[6];
                bool chosen_neg[6];

                for (size_t p_idx = 0; p_idx < n_pulses; p_idx++) {
                    size_t best_slot = (size_t)-1;
                    float best_c = 0.0f;
                    for (size_t slot = 0; slot < 30; slot++) {
                        if (used[slot]) continue;
                        size_t m = 2 * slot + (size_t)grid;
                        float c = 0.0f;
                        for (size_t n = m; n < G723_SUBFRAME_SIZE; n++) {
                            c += res[n] * hh[n - m];
                        }
                        if (best_slot == (size_t)-1 || fabsf(c) > fabsf(best_c)) {
                            best_c = c;
                            best_slot = slot;
                        }
                    }
                    float amp = (best_c < 0.0f) ? -g : g;
                    size_t m = 2 * best_slot + (size_t)grid;
                    for (size_t n = m; n < G723_SUBFRAME_SIZE; n++) {
                        res[n] -= amp * hh[n - m];
                    }
                    used[best_slot] = true;
                    chosen_slot[p_idx] = best_slot;
                    chosen_neg[p_idx] = (best_c < 0.0f);
                }

                float err = 0.0f;
                for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
                    err += res[n] * res[n];
                }

                if (err < best_err) {
                    for (size_t i = 0; i < n_pulses - 1; i++) {
                        for (size_t k = i + 1; k < n_pulses; k++) {
                            if (chosen_slot[i] > chosen_slot[k]) {
                                size_t ts = chosen_slot[i];
                                chosen_slot[i] = chosen_slot[k];
                                chosen_slot[k] = ts;
                                bool tn = chosen_neg[i];
                                chosen_neg[i] = chosen_neg[k];
                                chosen_neg[k] = tn;
                            }
                        }
                    }
                    uint32_t psig = 0;
                    for (size_t k = 0; k < n_pulses; k++) {
                        if (chosen_neg[k]) {
                            psig |= (1u << (n_pulses - 1 - k));
                        }
                    }
                    uint32_t code = 0;
                    if (g723_fcbk_pack_positions(chosen_slot, n_pulses, &code)) {
                        best_err = err;
                        best_pos_code = code;
                        best_psig = psig;
                        best_grid = grid;
                        best_mg = mg;
                        best_train = train;
                    }
                }
            }
        }
    }

    *pos_code_out = best_pos_code;
    *psig_out = best_psig;
    *grid_out = best_grid;
    *mg_out = best_mg;
    *train_out = best_train;
}

static void acelp_spec_search(
    const float target[G723_SUBFRAME_SIZE],
    const float h[G723_SUBFRAME_SIZE],
    int32_t lag,
    size_t pgindex,
    uint32_t *loop4_budget,
    uint32_t *pos_out,
    uint32_t *psig_out,
    uint8_t *grid_out,
    size_t *mg_out
) {
    const size_t TRACKS = 4;
    const size_t SLOTS = 8;
    const size_t POS_EXT = G723_SUBFRAME_SIZE + 4;

    float h_enh[G723_SUBFRAME_SIZE];
    memcpy(h_enh, h, sizeof(float) * G723_SUBFRAME_SIZE);
    g723_acelp_enhanced_impulse_response(h_enh, G723_SUBFRAME_SIZE, lag, pgindex);

    float d[POS_EXT];
    memset(d, 0, sizeof(d));
    for (size_t j = 0; j < G723_SUBFRAME_SIZE; j++) {
        float acc = 0.0f;
        for (size_t n = j; n < G723_SUBFRAME_SIZE; n++) {
            acc += target[n] * h_enh[n - j];
        }
        d[j] = acc;
    }

    float sgn[POS_EXT];
    float dp[POS_EXT];
    for (size_t i = 0; i < POS_EXT; i++) {
        sgn[i] = 1.0f;
        dp[i] = 0.0f;
    }

    for (size_t j = 0; j < G723_SUBFRAME_SIZE / 2; j++) {
        float a = d[2 * j];
        float b = d[2 * j + 1];
        float pick = (fabsf(a) > fabsf(b)) ? a : b;
        float s = (pick < 0.0f) ? -1.0f : 1.0f;
        sgn[2 * j] = s;
        sgn[2 * j + 1] = s;
        dp[2 * j] = a * s;
        dp[2 * j + 1] = b * s;
    }

    const size_t HALF = G723_SUBFRAME_SIZE / 2 + 2;
    float phi[HALF][HALF];
    memset(phi, 0, sizeof(phi));

    for (size_t a = 0; a < G723_SUBFRAME_SIZE / 2; a++) {
        for (size_t b = a; b < G723_SUBFRAME_SIZE / 2; b++) {
            size_t i = 2 * a;
            size_t j = 2 * b;
            float acc = 0.0f;
            for (size_t n = j; n < G723_SUBFRAME_SIZE; n++) {
                acc += h_enh[n - i] * h_enh[n - j];
            }
            float v = acc * sgn[i] * sgn[j];
            phi[a][b] = v;
            phi[b][a] = v;
        }
    }

    float max3 = 0.0f;
    float av3 = 0.0f;
    for (size_t t = 0; t < 3; t++) {
        float mx = -1e30f;
        float sum = 0.0f;
        size_t cnt = 0;
        for (size_t k = 0; k < SLOTS; k++) {
            size_t p = 8 * k + 2 * t;
            if (p < G723_SUBFRAME_SIZE) {
                if (dp[p] > mx) mx = dp[p];
                sum += dp[p];
                cnt++;
            }
        }
        max3 += mx;
        av3 += (cnt > 0) ? (sum / (float)cnt) : 0.0f;
    }
    float thr3 = av3 + (max3 - av3) * 0.5f;

    float best_c2 = 0.0f;
    float best_eps = 0.0f;
    float best_c = 0.0f;
    size_t best_slots[4] = {0};
    uint8_t best_grid = 0;
    bool found = false;

    for (size_t k0 = 0; k0 < SLOTS; k0++) {
        size_t p0 = 8 * k0;
        float e0 = phi[p0 / 2][p0 / 2];
        for (size_t k1 = 0; k1 < SLOTS; k1++) {
            size_t p1 = 8 * k1 + 2;
            float e1 = e0 + phi[p1 / 2][p1 / 2] + 2.0f * phi[p0 / 2][p1 / 2];
            for (size_t k2 = 0; k2 < SLOTS; k2++) {
                size_t p2 = 8 * k2 + 4;
                float e2 = e1 + phi[p2 / 2][p2 / 2] + 2.0f * (phi[p0 / 2][p2 / 2] + phi[p1 / 2][p2 / 2]);
                bool charged = false;
                for (size_t g = 0; g < 2; g++) {
                    float c3 = dp[p0 + g] + dp[p1 + g] + dp[p2 + g];
                    if (fabsf(c3) <= thr3) continue;
                    if (!charged) {
                        if (*loop4_budget == 0) break;
                        (*loop4_budget)--;
                        charged = true;
                    }
                    for (size_t k3 = 0; k3 < SLOTS; k3++) {
                        size_t p3 = 8 * k3 + 6;
                        float c = c3 + dp[p3 + g];
                        float eps = e2 + phi[p3 / 2][p3 / 2] + 2.0f * (phi[p0 / 2][p3 / 2] + phi[p1 / 2][p3 / 2] + phi[p2 / 2][p3 / 2]);
                        if (eps <= 0.0f) continue;
                        float c2 = c * c;
                        if (!found || c2 * best_eps > best_c2 * eps) {
                            found = true;
                            best_c2 = c2;
                            best_eps = eps;
                            best_c = c;
                            best_slots[0] = k0;
                            best_slots[1] = k1;
                            best_slots[2] = k2;
                            best_slots[3] = k3;
                            best_grid = (uint8_t)g;
                        }
                    }
                }
            }
        }
    }

    if (!found) {
        *pos_out = 0;
        *psig_out = 0xF;
        *grid_out = 0;
        *mg_out = 0;
        return;
    }

    float flip = (best_c < 0.0f) ? -1.0f : 1.0f;
    int32_t signs[4];
    uint32_t positions[4];
    for (size_t t = 0; t < TRACKS; t++) {
        positions[t] = (uint32_t)best_slots[t];
        size_t p = 8 * best_slots[t] + 2 * t + (size_t)best_grid;
        float s = (p < G723_SUBFRAME_SIZE) ? (sgn[p] * flip) : 1.0f;
        signs[t] = (s < 0.0f) ? -1 : 1;
    }

    float v_unit[G723_SUBFRAME_SIZE];
    place_pulses(positions, signs, best_grid, v_unit);

    float y[G723_SUBFRAME_SIZE];
    conv_causal(v_unit, h_enh, G723_SUBFRAME_SIZE, y);

    float c_ty = 0.0f, e_yy = 0.0f;
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        c_ty += target[n] * y[n];
        e_yy += y[n] * y[n];
    }
    if (c_ty < 0.0f) {
        for (size_t t = 0; t < TRACKS; t++) {
            signs[t] = -signs[t];
        }
        c_ty = -c_ty;
    }

    float g_opt = (e_yy > 0.0f) ? (c_ty / e_yy) : 0.0f;
    size_t mg = g723_nearest_fcb_gain(g_opt);

    uint32_t pos = positions[0] | (positions[1] << 3) | (positions[2] << 6) | (positions[3] << 9);
    uint32_t psig = 0;
    for (size_t t = 0; t < TRACKS; t++) {
        if (signs[t] > 0) {
            psig |= (1u << t);
        }
    }

    *pos_out = pos;
    *psig_out = psig;
    *grid_out = best_grid;
    *mg_out = mg;
}

static inline uint32_t encode_abs_lag(int32_t lag) {
    int32_t v = lag - (int32_t)G723_PITCH_MIN;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    return (uint32_t)v;
}

static inline uint32_t encode_delta_lag(int32_t lag, int32_t prev_lag) {
    int32_t d = lag - prev_lag;
    if (d < -1) d = -1;
    if (d > 2) d = 2;
    return (uint32_t)((d + 1) & 0x3);
}

void g723_encoder_analyse_frame(
    g723_encoder_state_t *state,
    const int16_t pcm_in[G723_FRAME_SIZE_SAMPLES],
    g723_frame_params_t *params
) {
    if (state == NULL || pcm_in == NULL || params == NULL) return;
    memset(params, 0, sizeof(g723_frame_params_t));
    params->rate = state->rate;

    int16_t frame[G723_FRAME_SIZE_SAMPLES];
    memcpy(&frame[0], state->framer_delay, sizeof(int16_t) * G723_LOOKAHEAD_SAMPLES);
    memcpy(&frame[G723_LOOKAHEAD_SAMPLES], &pcm_in[0],
           sizeof(int16_t) * (G723_FRAME_SIZE_SAMPLES - G723_LOOKAHEAD_SAMPLES));
    memcpy(state->framer_delay, &pcm_in[G723_FRAME_SIZE_SAMPLES - G723_LOOKAHEAD_SAMPLES],
           sizeof(int16_t) * G723_LOOKAHEAD_SAMPLES);

    float sig[G723_FRAME_SIZE_SAMPLES];
    if (state->highpass) {
        for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) {
            int16_t yh = hp_step(&state->hp_x_prev, &state->hp_acc, frame[i]);
            sig[i] = (float)yh * (1.0f / 16384.0f);
        }
    } else {
        for (size_t i = 0; i < G723_FRAME_SIZE_SAMPLES; i++) {
            sig[i] = (float)frame[i] * (1.0f / 32768.0f);
        }
    }

    float la[G723_LOOKAHEAD_SAMPLES];
    if (state->highpass) {
        int16_t xp = state->hp_x_prev;
        int32_t acc = state->hp_acc;
        for (size_t i = 0; i < G723_LOOKAHEAD_SAMPLES; i++) {
            la[i] = (float)hp_step(&xp, &acc, state->framer_delay[i]) * (1.0f / 16384.0f);
        }
    } else {
        for (size_t i = 0; i < G723_LOOKAHEAD_SAMPLES; i++) {
            la[i] = (float)state->framer_delay[i] * (1.0f / 32768.0f);
        }
    }

    float wind_buf[G723_SUBFRAME_SIZE + G723_FRAME_SIZE_SAMPLES + G723_LOOKAHEAD_SAMPLES];
    memcpy(&wind_buf[0], state->lpc_tail, sizeof(float) * G723_SUBFRAME_SIZE);
    memcpy(&wind_buf[G723_SUBFRAME_SIZE], sig, sizeof(float) * G723_FRAME_SIZE_SAMPLES);
    memcpy(&wind_buf[G723_SUBFRAME_SIZE + G723_FRAME_SIZE_SAMPLES], la, sizeof(float) * G723_LOOKAHEAD_SAMPLES);
    memcpy(state->lpc_tail, &sig[G723_FRAME_SIZE_SAMPLES - G723_SUBFRAME_SIZE], sizeof(float) * G723_SUBFRAME_SIZE);

    float a_unq[G723_SUBFRAMES_PER_FRAME][G723_LPC_ORDER + 1];
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        lpc_analysis(&wind_buf[s * G723_SUBFRAME_SIZE], a_unq[s]);
    }

    float wbuf[G723_WSPEECH_HIST_LEN + G723_FRAME_SIZE_SAMPLES];
    memcpy(&wbuf[0], state->wspeech_hist, sizeof(float) * G723_WSPEECH_HIST_LEN);

    float wtaps_z[G723_SUBFRAMES_PER_FRAME][G723_LPC_ORDER];
    float wtaps_p[G723_SUBFRAMES_PER_FRAME][G723_LPC_ORDER];
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        weighting_taps(a_unq[s], wtaps_z[s], wtaps_p[s]);
        size_t start = s * G723_SUBFRAME_SIZE;
        weight_subframe(&sig[start], wtaps_z[s], wtaps_p[s],
                        state->wght_x_mem, state->wght_f_mem,
                        &wbuf[G723_WSPEECH_HIST_LEN + start]);
    }
    memcpy(state->wspeech_hist, &wbuf[G723_FRAME_SIZE_SAMPLES], sizeof(float) * G723_WSPEECH_HIST_LEN);

    int32_t ol_lags[2] = {
        open_loop_pitch(wbuf, 0),
        open_loop_pitch(wbuf, 1)
    };

    float a3_exp[G723_LPC_ORDER + 1];
    memcpy(a3_exp, a_unq[G723_SUBFRAMES_PER_FRAME - 1], sizeof(float) * (G723_LPC_ORDER + 1));
    for (size_t k = 0; k < G723_LPC_ORDER; k++) {
        a3_exp[k + 1] *= (float)G723_LPC_BANDWIDTH_EXPANSION_Q15[k] / 32768.0f;
    }

    float lsp_cur_cos[G723_LPC_ORDER];
    if (!g723_lpc_to_lsp(a3_exp, lsp_cur_cos)) {
        memcpy(lsp_cur_cos, state->prev_unq_lsp, sizeof(float) * G723_LPC_ORDER);
    }
    memcpy(state->prev_unq_lsp, lsp_cur_cos, sizeof(float) * G723_LPC_ORDER);

    float lsp_cur_freq[G723_LPC_ORDER];
    g723_lsp_cosines_to_freq(lsp_cur_cos, lsp_cur_freq);

    float decoded_freq[G723_LPC_ORDER];
    params->lsp_index = g723_quantise_lsp_freq(lsp_cur_freq, state->shadow_prev_lsp_freq, decoded_freq);

    float cos_raw[G723_LPC_ORDER];
    g723_lsp_freq_to_cosines(decoded_freq, cos_raw);

    float lsp_q[G723_LPC_ORDER];
    if (!g723_enforce_lsp_stability(cos_raw, G723_LSP_STABILITY_DELTA_MIN_HZ, lsp_q)) {
        memcpy(lsp_q, state->shadow_prev_lsp, sizeof(float) * G723_LPC_ORDER);
    }

    float prev_lsp_snapshot[G723_LPC_ORDER];
    memcpy(prev_lsp_snapshot, state->shadow_prev_lsp, sizeof(float) * G723_LPC_ORDER);

    uint32_t acelp_budget = G723_ACELP_LOOP4_FRAME_BUDGET;
    int32_t lags[G723_SUBFRAMES_PER_FRAME];

    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        float lsp_interp[G723_LPC_ORDER];
        g723_interpolate_lsp(s, prev_lsp_snapshot, lsp_q, lsp_interp);

        float a_sub[G723_LPC_ORDER + 1];
        g723_lsp_to_lpc(lsp_interp, a_sub);

        size_t start = s * G723_SUBFRAME_SIZE;
        int32_t ol = ol_lags[s / 2];

        hns_params_t hns = harmonic_noise_shaping(wbuf, G723_WSPEECH_HIST_LEN + start, ol);

        float w_target[G723_SUBFRAME_SIZE];
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            size_t idx = G723_WSPEECH_HIST_LEN + start + n;
            w_target[n] = wbuf[idx] - hns.beta * wbuf[idx - (size_t)hns.lag];
        }

        float h[G723_SUBFRAME_SIZE];
        combined_impulse_response(a_sub, wtaps_z[s], wtaps_p[s], hns, h);

        g723_combined_mem_t zir_mem = state->comb_mem;
        float zero_in[G723_SUBFRAME_SIZE] = {0};
        float zir[G723_SUBFRAME_SIZE];
        run_combined(&zir_mem, zero_in, a_sub, wtaps_z[s], wtaps_p[s], hns, zir);

        float target[G723_SUBFRAME_SIZE];
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            target[n] = w_target[n] - zir[n];
        }

        int32_t candidates[4];
        size_t n_cands = 0;
        if (s % 2 == 0) {
            for (int32_t d = -1; d <= 1; d++) {
                int32_t l = ol + d;
                if (l >= (int32_t)G723_PITCH_MIN && l <= (int32_t)G723_PITCH_MAX) {
                    candidates[n_cands++] = l;
                }
            }
        } else {
            for (int32_t d = -1; d <= 2; d++) {
                int32_t l = lags[s - 1] + d;
                if (l >= (int32_t)G723_PITCH_MIN && l <= (int32_t)G723_PITCH_MAX) {
                    candidates[n_cands++] = l;
                }
            }
        }
        if (n_cands == 0) {
            candidates[n_cands++] = (int32_t)G723_PITCH_MIN;
        }

        float best_score = -1e30f;
        int32_t best_lag = candidates[0];
        size_t best_pg = 0;

        for (size_t c_idx = 0; c_idx < n_cands; c_idx++) {
            int32_t cand = candidates[c_idx];
            int32_t lag_base = (s % 2 == 0) ? cand : lags[s - 1];
            size_t rows = g723_gain_vq_rows(state->rate, lag_base);

            float basis[G723_ACB_TAPS][G723_SUBFRAME_SIZE];
            acb_basis_float(state->shadow_exc_history, G723_SHADOW_EXC_HIST_LEN, cand, basis);

            float y[G723_ACB_TAPS][G723_SUBFRAME_SIZE];
            for (size_t j = 0; j < G723_ACB_TAPS; j++) {
                conv_causal(basis[j], h, G723_SUBFRAME_SIZE, y[j]);
            }

            float d_vec[G723_ACB_TAPS] = {0};
            float rmat[G723_ACB_TAPS][G723_ACB_TAPS] = {{0}};
            for (size_t j = 0; j < G723_ACB_TAPS; j++) {
                for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
                    d_vec[j] += target[n] * y[j][n];
                }
                for (size_t k = j; k < G723_ACB_TAPS; k++) {
                    float acc = 0.0f;
                    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
                        acc += y[j][n] * y[k][n];
                    }
                    rmat[j][k] = acc;
                    rmat[k][j] = acc;
                }
            }

            float corr[G723_ACB_ROW_TERMS];
            memcpy(&corr[0], d_vec, sizeof(float) * G723_ACB_TAPS);
            for (size_t j = 0; j < G723_ACB_TAPS; j++) {
                corr[G723_ACB_TAPS + j] = 0.5f * rmat[j][j];
            }
            size_t k_idx = 2 * G723_ACB_TAPS;
            for (size_t j = 1; j < G723_ACB_TAPS; j++) {
                for (size_t i = 0; i < j; i++) {
                    corr[k_idx++] = rmat[i][j];
                }
            }

            for (size_t pg = 0; pg < rows; pg++) {
                float score = g723_acb_row_score(state->rate, lag_base, pg, corr);
                if (score > best_score) {
                    best_score = score;
                    best_lag = cand;
                    best_pg = pg;
                }
            }
        }

        params->acl[s] = (s % 2 == 0) ? encode_abs_lag(best_lag) : encode_delta_lag(best_lag, lags[s - 1]);
        lags[s] = best_lag;

        int32_t lag_base = (s < 2) ? lags[0] : lags[2];
        float taps[G723_ACB_TAPS];
        acb_taps_f32(state->rate, lag_base, best_pg, taps);

        float u[G723_SUBFRAME_SIZE];
        acb_contribution_f32(state->shadow_exc_history, G723_SHADOW_EXC_HIST_LEN, best_lag, taps, u);

        float u_filt[G723_SUBFRAME_SIZE];
        conv_causal(u, h, G723_SUBFRAME_SIZE, u_filt);

        float target2[G723_SUBFRAME_SIZE];
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            target2[n] = target[n] - u_filt[n];
        }

        uint32_t pos = 0, psig = 0;
        uint8_t grid = 0;
        size_t mg = 0;
        bool train = false;

        if (state->rate == G723_RATE_HIGH) {
            size_t n_pulses = (s % 2 == 0) ? 6 : 5;
            mpmlq_spec_search(target2, h, n_pulses, lag_base, &pos, &psig, &grid, &mg, &train);
            params->pos[s] = pos;
            params->psig[s] = psig;
            params->grid[s] = grid;
            params->gain[s] = g723_encode_gain_word(G723_RATE_HIGH, lag_base, best_pg, mg, train);
        } else {
            acelp_spec_search(target2, h, best_lag, best_pg, &acelp_budget, &pos, &psig, &grid, &mg);
            params->pos[s] = pos;
            params->psig[s] = psig;
            params->grid[s] = grid;
            params->gain[s] = g723_encode_gain_word(G723_RATE_LOW, lag_base, best_pg, mg, false);
        }

        g723_gain_info_t ginfo;
        g723_decode_gain_word(state->rate, lag_base, params->gain[s], &ginfo);
        float fcb_gain = (float)ginfo.fcb_gain / 32768.0f;

        float v[G723_SUBFRAME_SIZE];
        if (state->rate == G723_RATE_HIGH) {
            size_t n_pulses = (s % 2 == 0) ? 6 : 5;
            mpmlq_fixed_vector_f32(pos, psig, grid, n_pulses, fcb_gain, ginfo.train, lag_base, v);
        } else {
            acelp_fixed_vector_f32(pos, psig, grid, fcb_gain, v);
            acelp_pitch_enhance_f32(v, lags[s], ginfo.pgindex);
        }

        float exc[G723_SUBFRAME_SIZE];
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            exc[n] = u[n] + v[n];
        }

        memmove(&state->shadow_exc_history[0], &state->shadow_exc_history[G723_SUBFRAME_SIZE],
                sizeof(float) * (G723_SHADOW_EXC_HIST_LEN - G723_SUBFRAME_SIZE));
        memcpy(&state->shadow_exc_history[G723_SHADOW_EXC_HIST_LEN - G723_SUBFRAME_SIZE], exc,
               sizeof(float) * G723_SUBFRAME_SIZE);

        float dummy_out[G723_SUBFRAME_SIZE];
        run_combined(&state->comb_mem, exc, a_sub, wtaps_z[s], wtaps_p[s], hns, dummy_out);
    }

    memcpy(state->shadow_prev_lsp, lsp_q, sizeof(float) * G723_LPC_ORDER);
    g723_lsp_cosines_to_freq(lsp_q, state->shadow_prev_lsp_freq);
}

g723_result_t g723_encoder_encode_frame(
    g723_encoder_state_t *state,
    const int16_t pcm_in[G723_FRAME_SIZE_SAMPLES],
    uint8_t *output,
    size_t *output_size
) {
    if (state == NULL || pcm_in == NULL || output == NULL || output_size == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }
    size_t needed = (state->rate == G723_RATE_HIGH) ? G723_HIGH_RATE_BYTES : G723_LOW_RATE_BYTES;
    if (*output_size < needed) {
        *output_size = needed;
        return G723_RESULT_INVALID_ARGUMENT;
    }

    g723_frame_params_t params;
    g723_encoder_analyse_frame(state, pcm_in, &params);
    return g723_pack_frame(&params, output, *output_size, output_size);
}
