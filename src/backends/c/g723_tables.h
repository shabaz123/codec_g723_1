#ifndef G723_TABLES_H
#define G723_TABLES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "g723_consts.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const int16_t G723_HIGHPASS_FILTER_Q15[2];
extern const int16_t G723_LPC_HAMMING_WINDOW_Q15[180];
extern const int16_t G723_LPC_BINOMIAL_LAG_WINDOW_Q15[10];
extern const int16_t G723_LPC_BANDWIDTH_EXPANSION_Q15[10];
extern const int16_t G723_LSP_COSINE_LOOKUP_Q15[512];
extern const int16_t G723_LSP_DC_PREDICTED_FREQ_Q15[10];
extern const int16_t G723_LSP_BAND_INFO[6];
extern const int16_t G723_LSP_CODEBOOK_BAND0_Q13[768];
extern const int16_t G723_LSP_CODEBOOK_BAND1_Q13[768];
extern const int16_t G723_LSP_CODEBOOK_BAND2_Q13[1024];
extern const int16_t G723_PERCEPTUAL_ZERO_Q15[10];
extern const int16_t G723_PERCEPTUAL_POLE_Q15[10];
extern const int16_t G723_MPMLQ_PULSE_COUNT_PER_SUBFRAME[4];
extern const uint32_t G723_MPMLQ_MAX_POSITION[4];
extern const uint32_t G723_MPMLQ_COMBINATORIAL[180];
extern const int16_t G723_FIXED_CODEBOOK_GAIN_Q15[24];
extern const int16_t G723_ADAPTIVE_CODEBOOK_GAIN_5P3[1700];
extern const int16_t G723_ADAPTIVE_CODEBOOK_GAIN_6P3[3400];
extern const int16_t G723_GAIN_QUANTIZER_DECISION_FACTORS[4];
extern const int16_t G723_PITCH_1TAP_LTP_SELECTOR[170];
extern const int16_t G723_PITCH_1TAP_LTP_GAIN[170];
extern const int16_t G723_TAMING_GAIN_5P3[85];
extern const int16_t G723_TAMING_GAIN_6P3[170];
extern const int16_t G723_POSTFILTER_ZERO_Q15[10];
extern const int16_t G723_POSTFILTER_POLE_Q15[10];
extern const int16_t G723_BIT_ALLOCATION_SEGMENT_BASE[3];
extern const int32_t G723_BIT_ALLOCATION_SEGMENT_BOUNDARIES[3];
extern const uint32_t G723_ACELP_TRACK_BASES[4];

/* Accessors and helpers */
typedef struct {
    int16_t gain;      /* Q15 */
    int16_t selector;  /* offset -2..2, or 60 if disabled */
} g723_pitch_1tap_ltp_t;

bool g723_get_pitch_1tap_ltp(size_t pgindex, g723_pitch_1tap_ltp_t *out);

const int16_t *g723_lsp_codebook_entry(size_t band, uint8_t index, size_t *dim_out);

bool g723_acelp_track_position(size_t track, size_t slot, bool grid, size_t *pos_out);

bool g723_fcbk_unpk_positions(uint32_t index, size_t m, size_t positions[6]);
bool g723_fcbk_pack_positions(const size_t *positions, size_t m, uint32_t *index_out);

#ifdef __cplusplus
}
#endif

#endif /* G723_TABLES_H */
