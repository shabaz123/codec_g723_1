#include "g723_qdec.h"
#include "g723_tables.h"
#include "g723_spec_lsp.h"
#include "g723_spec_exc.h"
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

static const int32_t DELTA_LAG[4] = {-1, 0, 1, 2};

void g723_qdec_init(g723_qdec_state_t *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(g723_qdec_state_t));
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        state->prev_lsp[i] = G723_LSP_DC_PREDICTED_FREQ_Q15[i];
    }
    state->pf.agc_gain_q12 = 1 << 12;
    state->last_lag = 60;
    state->last_lag2 = 60;
    state->postfilter = true;
    state->clamp_syn_mem = true;
}

void g723_qdec_reset(g723_qdec_state_t *state) {
    if (state == NULL) {
        return;
    }
    bool pf = state->postfilter;
    bool cm = state->clamp_syn_mem;
    g723_qdec_init(state);
    state->postfilter = pf;
    state->clamp_syn_mem = cm;
}

void g723_qdec_set_postfilter(g723_qdec_state_t *state, bool enabled) {
    if (state != NULL) {
        state->postfilter = enabled;
    }
}

static void push_excitation(g723_qdec_state_t *state, const int32_t sub[G723_SUBFRAME_SIZE]) {
    memmove(&state->exc_hist[0], &state->exc_hist[G723_SUBFRAME_SIZE],
            sizeof(int32_t) * (G723_EXC_HIST_LEN - G723_SUBFRAME_SIZE));
    memcpy(&state->exc_hist[G723_EXC_HIST_LEN - G723_SUBFRAME_SIZE], sub,
           sizeof(int32_t) * G723_SUBFRAME_SIZE);
}

static void synthesis_subframe(
    const int16_t a[G723_LPC_ORDER],
    const int32_t x[G723_SUBFRAME_SIZE],
    int32_t mem[G723_LPC_ORDER],
    int16_t out[G723_SUBFRAME_SIZE],
    bool clamp_mem
) {
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        int64_t acc = (int64_t)x[n] << 13;
        for (size_t j = 0; j < G723_LPC_ORDER; j++) {
            acc += (int64_t)a[j] * (int64_t)mem[j];
        }
        int32_t y = sat32((acc + (1 << 12)) >> 13);
        int16_t y16 = g723_saturate16(y);
        for (int j = (int)G723_LPC_ORDER - 1; j > 0; j--) {
            mem[j] = mem[j - 1];
        }
        mem[0] = clamp_mem ? (int32_t)y16 : y;
        out[n] = y16;
    }
}

static inline int16_t ltp_gamma_q15(g723_rate_t rate) {
    return (rate == G723_RATE_HIGH) ? 6144 : 8192;
}

static void pitch_postfilter(
    const int32_t hist[G723_EXC_HIST_LEN],
    const int32_t frame[G723_FRAME_SIZE_SAMPLES],
    size_t start,
    int32_t ref_lag,
    g723_rate_t rate,
    int32_t ppf_out[G723_SUBFRAME_SIZE]
) {
    int32_t sf[G723_SUBFRAME_SIZE];
    memcpy(sf, &frame[start], sizeof(int32_t) * G723_SUBFRAME_SIZE);

    int32_t lag_c = ref_lag;
    if (lag_c < (int32_t)G723_PITCH_MIN) lag_c = (int32_t)G723_PITCH_MIN;
    if (lag_c > (int32_t)G723_PITCH_MAX) lag_c = (int32_t)G723_PITCH_MAX;

    int32_t m_lo = lag_c - 3;
    if (m_lo < 1) m_lo = 1;
    int32_t m_hi = lag_c + 3;

    int64_t t_en = 0;
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        t_en += (int64_t)sf[n] * (int64_t)sf[n];
    }
    if (t_en == 0) {
        memcpy(ppf_out, sf, sizeof(int32_t) * G723_SUBFRAME_SIZE);
        return;
    }

    size_t hlen = G723_EXC_HIST_LEN;

    // Forward search
    bool has_f = false;
    size_t best_f_m = 0;
    int64_t best_f_c = 0, best_f_d = 0;
    double best_f_metric = -1.0;

    for (int32_t m = m_lo; m <= m_hi; m++) {
        size_t mu = (size_t)m;
        if (start + G723_SUBFRAME_SIZE - 1 + mu >= G723_FRAME_SIZE_SAMPLES) {
            continue;
        }
        int64_t c = 0, d = 0;
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            int64_t x = (int64_t)frame[start + n + mu];
            c += (int64_t)sf[n] * x;
            d += x * x;
        }
        if (c > 0 && d > 0) {
            double metric = ((double)c * (double)c) / (double)d;
            if (!has_f || metric > best_f_metric) {
                has_f = true;
                best_f_metric = metric;
                best_f_m = mu;
                best_f_c = c;
                best_f_d = d;
            }
        }
    }

    // Backward search
    bool has_b = false;
    size_t best_b_m = 0;
    int64_t best_b_c = 0, best_b_d = 0;
    double best_b_metric = -1.0;

    for (int32_t m = m_lo; m <= m_hi; m++) {
        size_t mu = (size_t)m;
        int64_t c = 0, d = 0;
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            int32_t gidx = (int32_t)(start + n) - (int32_t)mu;
            int64_t x;
            if (gidx >= 0) {
                x = (int64_t)frame[gidx];
            } else {
                size_t k = (size_t)(-gidx);
                x = (k <= hlen) ? (int64_t)hist[hlen - k] : 0;
            }
            c += (int64_t)sf[n] * x;
            d += x * x;
        }
        if (c > 0 && d > 0) {
            double metric = ((double)c * (double)c) / (double)d;
            if (!has_b || metric > best_b_metric) {
                has_b = true;
                best_b_metric = metric;
                best_b_m = mu;
                best_b_c = c;
                best_b_d = d;
            }
        }
    }

    if (!has_f && !has_b) {
        memcpy(ppf_out, sf, sizeof(int32_t) * G723_SUBFRAME_SIZE);
        return;
    }

    double mf = has_f ? best_f_metric : -1.0;
    double mb = has_b ? best_b_metric : -1.0;

    size_t m_best;
    int64_t c, d;
    bool forward;
    if (mf >= mb) {
        m_best = best_f_m;
        c = best_f_c;
        d = best_f_d;
        forward = true;
    } else {
        m_best = best_b_m;
        c = best_b_c;
        d = best_b_d;
        forward = false;
    }

    // Prediction-gain gate: skip unless 4*C^2 >= D * t_en
    if (4.0 * ((double)c * (double)c) < ((double)d * (double)t_en)) {
        memcpy(ppf_out, sf, sizeof(int32_t) * G723_SUBFRAME_SIZE);
        return;
    }

    int64_t g_q15 = (c >= d) ? 32767 : ((c << 15) / (d > 0 ? d : 1));
    if (g_q15 < 0) g_q15 = 0;
    if (g_q15 > 32767) g_q15 = 32767;

    int64_t gg_q15 = (g_q15 * (int64_t)ltp_gamma_q15(rate)) >> 15;

    int64_t den = 0;
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        int64_t x;
        if (forward) {
            x = (int64_t)frame[start + n + m_best];
        } else {
            int32_t gidx = (int32_t)(start + n) - (int32_t)m_best;
            if (gidx >= 0) {
                x = (int64_t)frame[gidx];
            } else {
                size_t k = (size_t)(-gidx);
                x = (k <= hlen) ? (int64_t)hist[hlen - k] : 0;
            }
        }
        int32_t v = sat32((int64_t)sf[n] + ((gg_q15 * x) >> 15));
        ppf_out[n] = v;
        den += (int64_t)v * (int64_t)v;
    }

    if (den < t_en || den == 0) {
        return;
    }

    double ratio = (double)t_en / (double)den;
    int64_t gp_q15 = (int64_t)(sqrt(ratio) * 32768.0);
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        ppf_out[n] = sat32(((int64_t)ppf_out[n] * gp_q15 + (1 << 14)) >> 15);
    }
}

static void formant_agc_subframe(
    g723_postfilter_state_t *pf,
    const int16_t a[G723_LPC_ORDER],
    const int16_t sy[G723_SUBFRAME_SIZE],
    int16_t out[G723_SUBFRAME_SIZE]
) {
    int16_t an[G723_LPC_ORDER];
    int16_t ad[G723_LPC_ORDER];
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        an[i] = g723_mult(a[i], G723_POSTFILTER_ZERO_Q15[i]);
        ad[i] = g723_mult(a[i], G723_POSTFILTER_POLE_Q15[i]);
    }

    int16_t after_formant[G723_SUBFRAME_SIZE];
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        int16_t x = sy[n];
        int64_t acc = (int64_t)x << 13;
        for (size_t k = 0; k < G723_LPC_ORDER; k++) {
            acc -= (int64_t)an[k] * (int64_t)pf->num_mem[k];
            acc += (int64_t)ad[k] * (int64_t)pf->den_mem[k];
        }
        int16_t y = g723_saturate16(sat32((acc + (1 << 12)) >> 13));
        for (int k = (int)G723_LPC_ORDER - 1; k > 0; k--) {
            pf->num_mem[k] = pf->num_mem[k - 1];
            pf->den_mem[k] = pf->den_mem[k - 1];
        }
        pf->num_mem[0] = x;
        pf->den_mem[0] = y;
        after_formant[n] = y;
    }

    int64_t r0 = 0, r1 = 0;
    for (size_t n = 1; n < G723_SUBFRAME_SIZE; n++) {
        r0 += (int64_t)sy[n] * (int64_t)sy[n];
        r1 += (int64_t)sy[n] * (int64_t)sy[n - 1];
    }
    r0 += (int64_t)sy[0] * (int64_t)sy[0];

    int32_t k_q15 = 0;
    if (r0 > 0) {
        int64_t val = (r1 << 15) / r0;
        if (val < -32768) val = -32768;
        if (val > 32767) val = 32767;
        k_q15 = (int32_t)val;
    }
    pf->tilt_k1 = g723_saturate16((3 * (int32_t)pf->tilt_k1 + k_q15 + 2) >> 2);
    int32_t mu_q15 = (int32_t)(pf->tilt_k1 >> 2);

    int16_t after_tilt[G723_SUBFRAME_SIZE];
    int16_t prev = pf->tilt_prev;
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        int16_t x = after_formant[n];
        after_tilt[n] = g723_saturate16((int32_t)x - ((mu_q15 * (int32_t)prev) >> 15));
        prev = x;
    }
    pf->tilt_prev = prev;

    int64_t e_in = 0, e_out = 0;
    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        e_in += (int64_t)sy[n] * (int64_t)sy[n];
        e_out += (int64_t)after_tilt[n] * (int64_t)after_tilt[n];
    }

    int32_t gs_q12 = 1 << 12;
    if (e_out != 0) {
        double ratio = (double)e_in / (double)e_out;
        int32_t root = (int32_t)(sqrt(ratio) * 4096.0);
        gs_q12 = (root < (1 << 20)) ? root : (1 << 20);
    }

    for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
        pf->agc_gain_q12 += (gs_q12 - pf->agc_gain_q12) >> 4;
        int64_t q = ((int64_t)after_tilt[n] * (int64_t)pf->agc_gain_q12 * 17 + (1 << 15)) >> 16;
        out[n] = g723_saturate16(sat32(q));
    }
}

static void record_last_frame(
    g723_qdec_state_t *state,
    const int32_t lags[G723_SUBFRAMES_PER_FRAME],
    int32_t taps_sum_q15,
    int32_t g2,
    int32_t g3
) {
    state->last_lag = lags[G723_SUBFRAMES_PER_FRAME - 1];
    state->last_lag2 = lags[2];
    if (taps_sum_q15 < 0) taps_sum_q15 = 0;
    if (taps_sum_q15 > 32767) taps_sum_q15 = 32767;
    state->last_taps_sum_q15 = taps_sum_q15;
    state->last_gain_unvoiced = (g2 + g3) / 2;
    state->erased_run = 0;
}

static void record_pcm_history(g723_qdec_state_t *state, const int16_t pcm[G723_FRAME_SIZE_SAMPLES]) {
    size_t tail = G723_FRAME_SIZE_SAMPLES - G723_ERASURE_CLASSIFIER_HISTORY_LEN;
    memcpy(state->pcm_hist, &pcm[tail], sizeof(int16_t) * G723_ERASURE_CLASSIFIER_HISTORY_LEN);
}

void g723_qdec_decode_params(
    g723_qdec_state_t *state,
    const g723_frame_params_t *p,
    int16_t pcm_out[G723_FRAME_SIZE_SAMPLES]
) {
    int16_t cur[G723_LPC_ORDER];
    g723_lsp_decode_q15(p->lsp_index, state->prev_lsp, cur);
    if (!g723_lsp_stability_q15(cur, G723_LSP_DELTA_MIN_Q15)) {
        memcpy(cur, state->prev_lsp, sizeof(int16_t) * G723_LPC_ORDER);
    }

    int32_t lag0 = (int32_t)p->acl[0] + 18;
    int32_t lag1 = lag0 + DELTA_LAG[p->acl[1] & 3];
    int32_t lag2 = (int32_t)p->acl[2] + 18;
    int32_t lag3 = lag2 + DELTA_LAG[p->acl[3] & 3];
    int32_t lags[G723_SUBFRAMES_PER_FRAME] = {lag0, lag1, lag2, lag3};

    int32_t hist_snapshot[G723_EXC_HIST_LEN];
    memcpy(hist_snapshot, state->exc_hist, sizeof(int32_t) * G723_EXC_HIST_LEN);

    int32_t exc[G723_FRAME_SIZE_SAMPLES];
    int32_t fcb_gains[G723_SUBFRAMES_PER_FRAME];
    int32_t last_taps_sum = 0;

    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        int32_t lag_base = (s < 2) ? lags[0] : lags[2];
        g723_gain_info_t g;
        g723_decode_gain_word(p->rate, lag_base, p->gain[s], &g);
        fcb_gains[s] = (int32_t)g.fcb_gain;

        int32_t tap_sum = 0;
        for (size_t j = 0; j < G723_ACB_TAPS; j++) {
            tap_sum += (int32_t)g.taps[j] * 2;
        }
        last_taps_sum = tap_sum;

        int32_t u[G723_SUBFRAME_SIZE];
        g723_acb_contribution(state->exc_hist, G723_EXC_HIST_LEN, lags[s], g.taps, u);

        int32_t v[G723_SUBFRAME_SIZE];
        if (p->rate == G723_RATE_HIGH) {
            size_t n_pulses = (s % 2 == 0) ? 6 : 5;
            g723_mpmlq_fixed_vector(p->pos[s], p->psig[s], p->grid[s], n_pulses, g.fcb_gain, g.train, lag_base, v);
        } else {
            g723_acelp_fixed_vector(p->pos[s], p->psig[s], p->grid[s], g.fcb_gain, v);
            g723_acelp_pitch_enhance(v, lags[s], g.pgindex);
        }

        size_t start = s * G723_SUBFRAME_SIZE;
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            int32_t e = sat32((int64_t)u[n] + (int64_t)v[n]);
            if (e > 32767) e = 32767;
            if (e < -32768) e = -32768;
            exc[start + n] = e;
        }
        push_excitation(state, &exc[start]);
    }

    int16_t lpc[G723_SUBFRAMES_PER_FRAME][G723_LPC_ORDER];
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        int16_t lsp_sub[G723_LPC_ORDER];
        g723_lsp_interpolate_q15(s, state->prev_lsp, cur, lsp_sub);
        g723_lsp_to_lpc_q13(lsp_sub, lpc[s]);
    }

    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        size_t start = s * G723_SUBFRAME_SIZE;
        int32_t x[G723_SUBFRAME_SIZE];
        if (state->postfilter) {
            int32_t ref_lag = (s < 2) ? lags[0] : lags[2];
            pitch_postfilter(hist_snapshot, exc, start, ref_lag, p->rate, x);
        } else {
            memcpy(x, &exc[start], sizeof(int32_t) * G723_SUBFRAME_SIZE);
        }

        int16_t sy[G723_SUBFRAME_SIZE];
        synthesis_subframe(lpc[s], x, state->syn_mem, sy, state->clamp_syn_mem);

        if (state->postfilter) {
            int16_t post[G723_SUBFRAME_SIZE];
            formant_agc_subframe(&state->pf, lpc[s], sy, post);
            memcpy(&pcm_out[start], post, sizeof(int16_t) * G723_SUBFRAME_SIZE);
        } else {
            memcpy(&pcm_out[start], sy, sizeof(int16_t) * G723_SUBFRAME_SIZE);
        }
    }

    memcpy(state->prev_lsp, cur, sizeof(int16_t) * G723_LPC_ORDER);
    record_last_frame(state, lags, last_taps_sum, fcb_gains[2], fcb_gains[3]);
    record_pcm_history(state, pcm_out);
}

static bool classify_erasure_voicing(const g723_qdec_state_t *state, int32_t *best_lag_out) {
    const int16_t *hist = state->pcm_hist;
    size_t n = G723_ERASURE_CLASSIFIER_HISTORY_LEN;
    int32_t centre = state->last_lag2;
    int32_t best_lag = centre;
    bool voiced = false;
    double best_r = -1.0;

    for (int32_t d = -3; d <= 3; d++) {
        int32_t lag = centre + d;
        if (lag < (int32_t)G723_PITCH_MIN) lag = (int32_t)G723_PITCH_MIN;
        if (lag > (int32_t)G723_PITCH_MAX) lag = (int32_t)G723_PITCH_MAX;
        if ((size_t)lag >= n) continue;

        int64_t c = 0, e = 0, t = 0;
        for (size_t k = (size_t)lag; k < n; k++) {
            int64_t curv = (int64_t)hist[k];
            int64_t prev = (int64_t)hist[k - (size_t)lag];
            c += curv * prev;
            e += prev * prev;
            t += curv * curv;
        }
        if (e == 0 || t == 0 || c <= 0) {
            continue;
        }
        double num = (double)c * (double)c;
        double den = (double)e * (double)t;
        if (den <= 0.0) den = 1.0;
        double r = num / den;
        if (r > best_r) {
            best_r = r;
            best_lag = lag;
            voiced = (num * 8.0 >= den);
        }
    }
    *best_lag_out = best_lag;
    return voiced;
}

void g723_qdec_decode_erased(
    g723_qdec_state_t *state,
    int16_t pcm_out[G723_FRAME_SIZE_SAMPLES]
) {
    state->erased_run++;

    int32_t atten_q15 = 32767;
    for (uint32_t i = 0; i < state->erased_run; i++) {
        atten_q15 = (atten_q15 * 24576) >> 15;
    }
    if (state->erased_run > G723_ERASURE_MUTE_AFTER_FRAMES) {
        atten_q15 = 0;
    }

    int16_t cur[G723_LPC_ORDER];
    g723_lsp_extrapolate_q15(state->prev_lsp, cur);
    if (!g723_lsp_stability_q15(cur, G723_LSP_DELTA_MIN_ERASURE_Q15)) {
        memcpy(cur, state->prev_lsp, sizeof(int16_t) * G723_LPC_ORDER);
    }

    int32_t class_lag = 0;
    bool voiced = classify_erasure_voicing(state, &class_lag);
    int32_t lag = voiced ? class_lag : state->last_lag;
    if (lag < (int32_t)G723_PITCH_MIN) lag = (int32_t)G723_PITCH_MIN;
    if (lag > (int32_t)G723_PITCH_MAX) lag = (int32_t)G723_PITCH_MAX;

    uint32_t lcg = 0xDEADBEEFu + state->erased_run * 0x9E3779B9u;
    int32_t g_adapt_q15 = (int32_t)(((int64_t)state->last_taps_sum_q15 * (int64_t)atten_q15) >> 15);
    int32_t g_unvoiced = (int32_t)(((int64_t)state->last_gain_unvoiced * (int64_t)atten_q15) >> 15);

    int16_t lpc[G723_SUBFRAMES_PER_FRAME][G723_LPC_ORDER];
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        int16_t lsp_sub[G723_LPC_ORDER];
        g723_lsp_interpolate_q15(s, state->prev_lsp, cur, lsp_sub);
        g723_lsp_to_lpc_q13(lsp_sub, lpc[s]);
    }

    size_t hlen = G723_EXC_HIST_LEN;
    size_t l = (size_t)lag;

    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        int32_t exc[G723_SUBFRAME_SIZE];
        if (voiced) {
            for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
                size_t idx = (l > n) ? (hlen - (l - n)) : (hlen - l + ((n - l) % l));
                exc[n] = sat32(((int64_t)state->exc_hist[idx] * (int64_t)g_adapt_q15) >> 15);
            }
        } else {
            for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
                lcg = lcg * 1664525u + 1013904223u;
                int32_t rand_q15 = (int32_t)((lcg >> 8) & 0xFFFFu) - 32768;
                exc[n] = sat32(((int64_t)g_unvoiced * (int64_t)rand_q15) >> 15);
            }
        }
        for (size_t n = 0; n < G723_SUBFRAME_SIZE; n++) {
            if (exc[n] > 32767) exc[n] = 32767;
            if (exc[n] < -32768) exc[n] = -32768;
        }
        push_excitation(state, exc);

        int16_t sy[G723_SUBFRAME_SIZE];
        synthesis_subframe(lpc[s], exc, state->syn_mem, sy, state->clamp_syn_mem);

        size_t start = s * G723_SUBFRAME_SIZE;
        if (state->postfilter) {
            int16_t post[G723_SUBFRAME_SIZE];
            formant_agc_subframe(&state->pf, lpc[s], sy, post);
            memcpy(&pcm_out[start], post, sizeof(int16_t) * G723_SUBFRAME_SIZE);
        } else {
            memcpy(&pcm_out[start], sy, sizeof(int16_t) * G723_SUBFRAME_SIZE);
        }
    }

    memcpy(state->prev_lsp, cur, sizeof(int16_t) * G723_LPC_ORDER);
    record_pcm_history(state, pcm_out);
}

g723_result_t g723_qdec_decode_frame(
    g723_qdec_state_t *state,
    const uint8_t *input,
    size_t input_size,
    int16_t pcm_out[G723_FRAME_SIZE_SAMPLES]
) {
    if (state == NULL || input == NULL || pcm_out == NULL || input_size == 0) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    if (input_size == G723_SID_BYTES || input_size == G723_UNTRANSMITTED_BYTES) {
        g723_qdec_decode_erased(state, pcm_out);
        return G723_RESULT_OK;
    }

    g723_frame_params_t params;
    g723_result_t res = g723_unpack_frame(input, input_size, &params);
    if (res != G723_RESULT_OK) {
        return res;
    }

    g723_qdec_decode_params(state, &params, pcm_out);
    return G723_RESULT_OK;
}
