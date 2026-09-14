#include "g723_spec_lsp.h"
#include "g723_tables.h"
#include "g723_basicop.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void g723_split_lsp_index(uint32_t lsp_index, uint8_t bands[3]) {
    bands[0] = (uint8_t)((lsp_index >> 16) & 0xFF);
    bands[1] = (uint8_t)((lsp_index >> 8) & 0xFF);
    bands[2] = (uint8_t)(lsp_index & 0xFF);
}

uint32_t g723_combine_lsp_index(const uint8_t bands[3]) {
    return ((uint32_t)bands[0] << 16) | ((uint32_t)bands[1] << 8) | (uint32_t)bands[2];
}

void g723_lsp_dc_freq(float freq[G723_LPC_ORDER]) {
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        freq[i] = (float)G723_LSP_DC_PREDICTED_FREQ_Q15[i];
    }
}

void g723_lsp_freq_to_cosines(const float freq[G723_LPC_ORDER], float cos_out[G723_LPC_ORDER]) {
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        cos_out[i] = (float)cos(M_PI * (double)freq[i] / 32768.0);
    }
}

void g723_lsp_cosines_to_freq(const float cos_in[G723_LPC_ORDER], float freq_out[G723_LPC_ORDER]) {
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        double c = (double)cos_in[i];
        if (c > 1.0) c = 1.0;
        if (c < -1.0) c = -1.0;
        freq_out[i] = (float)(acos(c) * 32768.0 / M_PI);
    }
}

void g723_decode_lsp_freq(
    uint32_t lsp_index,
    const float prev[G723_LPC_ORDER],
    float out[G723_LPC_ORDER]
) {
    uint8_t bands[3];
    g723_split_lsp_index(lsp_index, bands);

    float dc[G723_LPC_ORDER];
    g723_lsp_dc_freq(dc);

    for (size_t m = 0; m < 3; m++) {
        size_t dim = 0;
        const int16_t *row = g723_lsp_codebook_entry(m, bands[m], &dim);
        size_t start = (m == 0) ? 0 : ((m == 1) ? 3 : 6);
        for (size_t j = 0; j < dim; j++) {
            size_t i = start + j;
            float predicted = G723_LSP_PREDICTOR_B * (prev[i] - dc[i]);
            out[i] = predicted + dc[i] + (float)row[j];
        }
    }
}

static void lsp_weights(const float p_unq[G723_LPC_ORDER], float w[G723_LPC_ORDER]) {
    for (size_t j = 0; j < G723_LPC_ORDER; j++) {
        float lower = (j > 0) ? (p_unq[j] - p_unq[j - 1]) : 1e30f;
        float upper = (j + 1 < G723_LPC_ORDER) ? (p_unq[j + 1] - p_unq[j]) : 1e30f;
        float gap = (lower < upper) ? lower : upper;
        if (gap < 1.0f) {
            gap = 1.0f;
        }
        w[j] = 1.0f / gap;
    }
}

uint32_t g723_quantise_lsp_freq(
    const float p_unq[G723_LPC_ORDER],
    const float prev[G723_LPC_ORDER],
    float decoded_out[G723_LPC_ORDER]
) {
    float dc[G723_LPC_ORDER];
    g723_lsp_dc_freq(dc);

    float w[G723_LPC_ORDER];
    lsp_weights(p_unq, w);

    float e_target[G723_LPC_ORDER];
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        e_target[i] = p_unq[i] - dc[i] - G723_LSP_PREDICTOR_B * (prev[i] - dc[i]);
    }

    uint8_t bands[3] = {0, 0, 0};

    for (size_t m = 0; m < 3; m++) {
        size_t start = (m == 0) ? 0 : ((m == 1) ? 3 : 6);
        size_t dim = (m == 2) ? 4 : 3;

        uint8_t best_idx = 0;
        float best_err = 1e30f;

        for (size_t idx = 0; idx < 256; idx++) {
            size_t dummy_dim = 0;
            const int16_t *row = g723_lsp_codebook_entry(m, (uint8_t)idx, &dummy_dim);
            float err = 0.0f;
            for (size_t j = 0; j < dim; j++) {
                size_t i = start + j;
                float diff = e_target[i] - (float)row[j];
                err += w[i] * diff * diff;
            }
            if (err < best_err) {
                best_err = err;
                best_idx = (uint8_t)idx;
            }
        }
        bands[m] = best_idx;
    }

    uint32_t lsp_index = g723_combine_lsp_index(bands);
    if (decoded_out != NULL) {
        g723_decode_lsp_freq(lsp_index, prev, decoded_out);
    }
    return lsp_index;
}

void g723_lsp_decode_q15(
    uint32_t lsp_index,
    const int16_t prev[G723_LPC_ORDER],
    int16_t out[G723_LPC_ORDER]
) {
    uint8_t bands[3];
    g723_split_lsp_index(lsp_index, bands);

    for (size_t m = 0; m < 3; m++) {
        size_t dim = 0;
        const int16_t *row = g723_lsp_codebook_entry(m, bands[m], &dim);
        size_t start = (m == 0) ? 0 : ((m == 1) ? 3 : 6);
        for (size_t j = 0; j < dim; j++) {
            size_t i = start + j;
            int16_t dc = G723_LSP_DC_PREDICTED_FREQ_Q15[i];
            int16_t pred = g723_mult(g723_sub(prev[i], dc), G723_LSP_PREDICTOR_B_Q15);
            out[i] = g723_add(g723_add(dc, pred), row[j]);
        }
    }
}

void g723_lsp_extrapolate_q15(
    const int16_t prev[G723_LPC_ORDER],
    int16_t out[G723_LPC_ORDER]
) {
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        int16_t dc = G723_LSP_DC_PREDICTED_FREQ_Q15[i];
        int16_t pred = g723_mult(g723_sub(prev[i], dc), G723_LSP_PREDICTOR_BE_Q15);
        out[i] = g723_add(dc, pred);
    }
}

bool g723_lsp_stability_q15(
    int16_t p[G723_LPC_ORDER],
    int16_t delta_min
) {
    int16_t half = delta_min / 2;
    for (size_t iter = 0; iter < G723_LSP_STABILITY_MAX_ITERATIONS; iter++) {
        bool violated = false;
        for (size_t j = 0; j < G723_LPC_ORDER - 1; j++) {
            int32_t diff = (int32_t)p[j + 1] - (int32_t)p[j];
            if (diff < (int32_t)delta_min) {
                int16_t avg = (int16_t)(((int32_t)p[j] + (int32_t)p[j + 1]) >> 1);
                p[j] = g723_sub(avg, half);
                p[j + 1] = g723_add(avg, half);
                violated = true;
            }
        }
        if (!violated) {
            return true;
        }
    }
    for (size_t j = 0; j < G723_LPC_ORDER - 1; j++) {
        if ((int32_t)p[j + 1] - (int32_t)p[j] < (int32_t)delta_min) {
            return false;
        }
    }
    return true;
}

void g723_lsp_interpolate_q15(
    size_t subframe,
    const int16_t prev[G723_LPC_ORDER],
    const int16_t cur[G723_LPC_ORDER],
    int16_t out[G723_LPC_ORDER]
) {
    if (subframe >= 3) {
        memcpy(out, cur, sizeof(int16_t) * G723_LPC_ORDER);
        return;
    }
    int16_t wp, wc;
    switch (subframe) {
        case 0:  wp = 24576; wc = 8192;  break;
        case 1:  wp = 16384; wc = 16384; break;
        default: wp = 8192;  wc = 24576; break;
    }
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        int32_t acc = g723_l_mac(g723_l_mult(prev[i], wp), cur[i], wc);
        out[i] = g723_round16(acc);
    }
}

int16_t g723_cos_q14(int16_t freq_q15) {
    int32_t f = freq_q15 < 0 ? 0 : (int32_t)freq_q15;
    size_t idx = (size_t)(f >> 7);
    int32_t frac = f & 0x7F;
    int32_t p0 = (int32_t)G723_LSP_COSINE_LOOKUP_Q15[idx];
    int32_t p1 = (int32_t)G723_LSP_COSINE_LOOKUP_Q15[idx + 1];
    int32_t diff = p1 - p0;
    return (int16_t)(p0 + ((diff * frac) >> 7));
}

void g723_lsp_to_lpc_q13(
    const int16_t lsp_freq[G723_LPC_ORDER],
    int16_t a_out[G723_LPC_ORDER]
) {
    const int64_t ONE_Q24 = (int64_t)1 << 24;
    int64_t pz[G723_LPC_ORDER + 1] = {0};
    int64_t qz[G723_LPC_ORDER + 1] = {0};
    pz[0] = ONE_Q24;
    qz[0] = ONE_Q24;

    size_t deg = 0;
    for (size_t k = 0; k < G723_LPC_ORDER / 2; k++) {
        int64_t c_even = (int64_t)g723_cos_q14(lsp_freq[2 * k]);
        int64_t c_odd  = (int64_t)g723_cos_q14(lsp_freq[2 * k + 1]);
        deg += 2;
        for (size_t i = deg; i >= 2; i--) {
            pz[i] += -((pz[i - 1] * c_even) >> 13) + pz[i - 2];
            qz[i] += -((qz[i - 1] * c_odd) >> 13) + qz[i - 2];
        }
        pz[1] -= (pz[0] * c_even) >> 13;
        qz[1] -= (qz[0] * c_odd) >> 13;
    }

    for (size_t j = 1; j <= G723_LPC_ORDER; j++) {
        int64_t sum = pz[j] + pz[j - 1] + qz[j] - qz[j - 1];
        int64_t neg = -(sum >> 1);
        int64_t q13 = (neg + ((int64_t)1 << 10)) >> 11;
        if (q13 > 32767) {
            a_out[j - 1] = 32767;
        } else if (q13 < -32768) {
            a_out[j - 1] = -32768;
        } else {
            a_out[j - 1] = (int16_t)q13;
        }
    }
}

void g723_lsp_to_lpc(
    const float lsp_cos[G723_LPC_ORDER],
    float a_out[G723_LPC_ORDER + 1]
) {
    float pz[G723_LPC_ORDER + 1] = {0};
    float qz[G723_LPC_ORDER + 1] = {0};
    pz[0] = 1.0f;
    qz[0] = 1.0f;

    size_t pz_deg = 0;
    size_t qz_deg = 0;

    for (size_t k = 0; k < G723_LPC_ORDER / 2; k++) {
        float lsp_even = lsp_cos[2 * k];
        float lsp_odd  = lsp_cos[2 * k + 1];

        pz_deg += 2;
        for (size_t i = pz_deg; i >= 2; i--) {
            pz[i] += -2.0f * lsp_even * pz[i - 1] + pz[i - 2];
        }
        pz[1] -= 2.0f * lsp_even * pz[0];

        qz_deg += 2;
        for (size_t i = qz_deg; i >= 2; i--) {
            qz[i] += -2.0f * lsp_odd * qz[i - 1] + qz[i - 2];
        }
        qz[1] -= 2.0f * lsp_odd * qz[0];
    }

    float f1[G723_LPC_ORDER + 2] = {0};
    float f2[G723_LPC_ORDER + 2] = {0};
    f1[0] = pz[0];
    f2[0] = qz[0];
    for (size_t i = 1; i <= G723_LPC_ORDER; i++) {
        f1[i] = pz[i] + pz[i - 1];
        f2[i] = qz[i] - qz[i - 1];
    }
    f1[G723_LPC_ORDER + 1] = pz[G723_LPC_ORDER];
    f2[G723_LPC_ORDER + 1] = -qz[G723_LPC_ORDER];

    a_out[0] = 1.0f;
    for (size_t i = 1; i <= G723_LPC_ORDER; i++) {
        a_out[i] = 0.5f * (f1[i] + f2[i]);
    }
}

static size_t cheby_roots(const float coeffs[6], float roots[5]) {
    double c[6];
    c[0] = (double)coeffs[5] * 0.5;
    for (size_t m = 1; m <= 5; m++) {
        c[m] = (double)coeffs[5 - m];
    }

    // Clenshaw recurrence evaluation
    #define EVAL_CHEBY(x_val) ({ \
        double x_ = (x_val); \
        double b2_ = 0.0, b1_ = 0.0; \
        for (int k_ = 5; k_ >= 1; k_--) { \
            double b0_ = 2.0 * x_ * b1_ - b2_ + c[k_]; \
            b2_ = b1_; \
            b1_ = b0_; \
        } \
        x_ * b1_ - b2_ + c[0]; \
    })

    const size_t GRID = 1024;
    size_t root_count = 0;
    double prev_x = 1.0;
    double prev_y = EVAL_CHEBY(prev_x);

    for (size_t i = 1; i <= GRID; i++) {
        double x = cos(M_PI * (double)i / (double)GRID);
        double y = EVAL_CHEBY(x);

        if (prev_y == 0.0) {
            roots[root_count++] = (float)prev_x;
        } else if (prev_y * y < 0.0) {
            double lo = x;
            double hi = prev_x;
            double flo = y;
            for (int it = 0; it < 50; it++) {
                double mid = 0.5 * (lo + hi);
                double fm = EVAL_CHEBY(mid);
                if (fm * flo < 0.0) {
                    hi = mid;
                } else {
                    lo = mid;
                    flo = fm;
                }
            }
            roots[root_count++] = (float)(0.5 * (lo + hi));
        }

        if (root_count == 5) {
            break;
        }
        prev_x = x;
        prev_y = y;
    }
    #undef EVAL_CHEBY

    return root_count;
}

bool g723_lpc_to_lsp(
    const float a[G723_LPC_ORDER + 1],
    float lsp_cos_out[G723_LPC_ORDER]
) {
    float f1[6];
    float f2[6];
    f1[0] = 1.0f;
    f2[0] = 1.0f;
    float prev_f1 = f1[0];
    float prev_f2 = f2[0];

    for (size_t i = 1; i <= 5; i++) {
        float ai = a[i];
        float api = a[11 - i];
        f1[i] = ai + api - prev_f1;
        f2[i] = ai - api + prev_f2;
        prev_f1 = f1[i];
        prev_f2 = f2[i];
    }

    float roots_f1[5] = {0};
    float roots_f2[5] = {0};
    if (cheby_roots(f1, roots_f1) != 5 || cheby_roots(f2, roots_f2) != 5) {
        return false;
    }

    for (size_t k = 0; k < 5; k++) {
        lsp_cos_out[2 * k]     = roots_f1[k];
        lsp_cos_out[2 * k + 1] = roots_f2[k];
    }

    for (size_t k = 1; k < G723_LPC_ORDER; k++) {
        if (lsp_cos_out[k] >= lsp_cos_out[k - 1]) {
            return false;
        }
    }
    return true;
}

void g723_lsp_dc_cosines(float lsp_cos[G723_LPC_ORDER]) {
    float dc_freq[G723_LPC_ORDER];
    g723_lsp_dc_freq(dc_freq);
    g723_lsp_freq_to_cosines(dc_freq, lsp_cos);
}

bool g723_enforce_lsp_stability(
    const float lsp_cos[G723_LPC_ORDER],
    float delta_min_hz,
    float out[G723_LPC_ORDER]
) {
    float omega[G723_LPC_ORDER];
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        float c = lsp_cos[i];
        if (c < -1.0f) c = -1.0f;
        if (c > 1.0f) c = 1.0f;
        omega[i] = acosf(c);
    }
    float delta_min_rad = (float)(2.0 * M_PI * (double)delta_min_hz / (double)G723_SAMPLE_RATE_HZ);
    float half = 0.5f * delta_min_rad;
    float tol = delta_min_rad * 1.0e-5f;
    bool converged = false;

    for (size_t iter = 0; iter < G723_LSP_STABILITY_MAX_ITERATIONS; iter++) {
        bool violated = false;
        for (size_t j = 0; j < G723_LPC_ORDER - 1; j++) {
            if (omega[j + 1] - omega[j] < delta_min_rad - tol) {
                float mid = 0.5f * (omega[j] + omega[j + 1]);
                omega[j] = mid - half;
                omega[j + 1] = mid + half;
                violated = true;
            }
        }
        if (!violated) {
            converged = true;
            break;
        }
    }

    float margin = half > 1.0e-3f ? half : 1.0e-3f;
    if (omega[0] < margin) {
        omega[0] = margin;
    }
    if (omega[G723_LPC_ORDER - 1] > (float)M_PI - margin) {
        omega[G723_LPC_ORDER - 1] = (float)M_PI - margin;
    }

    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        out[i] = cosf(omega[i]);
    }
    return converged;
}

void g723_interpolate_lsp(
    size_t subframe,
    const float prev[G723_LPC_ORDER],
    const float cur[G723_LPC_ORDER],
    float out[G723_LPC_ORDER]
) {
    float wp, wc;
    switch (subframe) {
        case 0:  wp = 0.75f; wc = 0.25f; break;
        case 1:  wp = 0.50f; wc = 0.50f; break;
        case 2:  wp = 0.25f; wc = 0.75f; break;
        default: wp = 0.00f; wc = 1.00f; break;
    }
    for (size_t i = 0; i < G723_LPC_ORDER; i++) {
        out[i] = wp * prev[i] + wc * cur[i];
    }
}

