#ifndef G723_CONSTS_H
#define G723_CONSTS_H

#include <stdint.h>
#include <stddef.h>

#define G723_SAMPLE_RATE_HZ             8000u
#define G723_FRAME_SIZE_SAMPLES         240u
#define G723_SUBFRAME_SIZE              60u
#define G723_SUBFRAMES_PER_FRAME        4u
#define G723_LPC_ORDER                  10u
#define G723_LPC_WINDOW                 180u
#define G723_PITCH_MIN                  18u
#define G723_PITCH_MAX                  142u
#define G723_LOOKAHEAD_SAMPLES          60u

#define G723_HIGH_RATE_BYTES            24u
#define G723_HIGH_RATE_BITS             189u
#define G723_LOW_RATE_BYTES             20u
#define G723_LOW_RATE_BITS              158u
#define G723_SID_BYTES                  4u
#define G723_UNTRANSMITTED_BYTES        1u

#define G723_LSP_VQ_SUBCODEBOOKS        3u
#define G723_LSP_VQ_ENTRIES             256u

#define G723_PERCEPTUAL_GAMMA1          0.9f
#define G723_PERCEPTUAL_GAMMA2          0.5f

#define G723_POSTFILTER_GAMMA1          0.65f
#define G723_POSTFILTER_GAMMA2          0.75f
#define G723_POSTFILTER_TILT_BASE       0.25f
#define G723_POSTFILTER_TILT_SMOOTH_ALPHA 0.25f
#define G723_POSTFILTER_AGC_ALPHA       (1.0f / 16.0f)
#define G723_POSTFILTER_AGC_INIT_GAIN   1.0f

#define G723_POSTFILTER_LTP_GAMMA_HIGH  0.1875f
#define G723_POSTFILTER_LTP_GAMMA_LOW   0.25f

#define G723_LSP_STABILITY_DELTA_MIN_HZ         31.25f
#define G723_LSP_STABILITY_DELTA_MIN_ERASURE_HZ 62.5f

#define G723_LSP_PREDICTOR_B            (12.0f / 32.0f)
#define G723_LSP_PREDICTOR_BE           (23.0f / 32.0f)
#define G723_LSP_STABILITY_MAX_ITERATIONS 10u

#define G723_POSTFILTER_LTP_PRED_GAIN_DB_MIN 1.25f
#define G723_POSTFILTER_LTP_SEARCH_RADIUS    3

#define G723_ERASURE_CLASSIFIER_HISTORY_LEN 120u
#define G723_ERASURE_CLASSIFIER_LAG_RADIUS  3
#define G723_ERASURE_VOICED_THRESHOLD_DB    0.58f
#define G723_ERASURE_ATTENUATION_DB_PER_FRAME 2.5f
#define G723_ERASURE_MUTE_AFTER_FRAMES      3u

#define G723_ACB_TAPS                   5u
#define G723_GAIN_ROWS_170              170u
#define G723_GAIN_ROWS_85               85u
#define G723_SHORT_LAG_LIMIT            58
#define G723_GAIN_TABLE_SIZE            24u
#define G723_ACB_ROW_TERMS              20u

#define G723_HP_POLE_Q15                32512
#define G723_LSP_PREDICTOR_B_Q15        12288
#define G723_LSP_PREDICTOR_BE_Q15       23552
#define G723_LSP_DELTA_MIN_Q15          256
#define G723_LSP_DELTA_MIN_ERASURE_Q15  512

#endif /* G723_CONSTS_H */
