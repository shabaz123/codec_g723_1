#ifndef G723_RESAMPLE_H
#define G723_RESAMPLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define G723_16K_SAMPLES_PER_FRAME 480u
#define G723_8K_SAMPLES_PER_FRAME  240u

#define G723_DOWN_FILTER_TAPS      31u
#define G723_DOWN_HIST_LEN         (G723_DOWN_FILTER_TAPS - 1u) /* 30 */

#define G723_UP_FILTER_TAPS_P0     16u
#define G723_UP_FILTER_TAPS_P1     15u
#define G723_UP_HIST_LEN           16u

typedef struct {
    int16_t down_hist[G723_DOWN_HIST_LEN];
    int16_t up_hist[G723_UP_HIST_LEN];
} g723_resampler_state_t;

void g723_resampler_init(g723_resampler_state_t *state);
void g723_resampler_reset(g723_resampler_state_t *state);

void g723_resample_16k_to_8k(
    g723_resampler_state_t *state,
    const int16_t in_16k[G723_16K_SAMPLES_PER_FRAME],
    int16_t out_8k[G723_8K_SAMPLES_PER_FRAME]
);

void g723_resample_8k_to_16k(
    g723_resampler_state_t *state,
    const int16_t in_8k[G723_8K_SAMPLES_PER_FRAME],
    int16_t out_16k[G723_16K_SAMPLES_PER_FRAME]
);

#ifdef __cplusplus
}
#endif

#endif /* G723_RESAMPLE_H */
