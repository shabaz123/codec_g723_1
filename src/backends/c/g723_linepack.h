#ifndef G723_LINEPACK_H
#define G723_LINEPACK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "codec_g723_1.h"
#include "g723_consts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    G723_RATE_HIGH = 0, /* 6.3 kbit/s MP-MLQ */
    G723_RATE_LOW  = 1  /* 5.3 kbit/s ACELP */
} g723_rate_t;

#define G723_MSBPOS_LIMIT 8100u

typedef struct {
    g723_rate_t rate;
    uint32_t lsp_index;
    uint32_t acl[G723_SUBFRAMES_PER_FRAME];
    uint32_t gain[G723_SUBFRAMES_PER_FRAME];
    uint8_t  grid[G723_SUBFRAMES_PER_FRAME];
    uint32_t pos[G723_SUBFRAMES_PER_FRAME];
    uint32_t psig[G723_SUBFRAMES_PER_FRAME];
} g723_frame_params_t;

uint32_t g723_msbpos_combine(const uint32_t pos[G723_SUBFRAMES_PER_FRAME]);
bool g723_msbpos_split(uint32_t word, uint32_t digits[G723_SUBFRAMES_PER_FRAME]);

bool g723_validate_frame_params(const g723_frame_params_t *params);

g723_result_t g723_pack_frame(
    const g723_frame_params_t *params,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size
);

g723_result_t g723_unpack_frame(
    const uint8_t *data,
    size_t size,
    g723_frame_params_t *params
);

#ifdef __cplusplus
}
#endif

#endif /* G723_LINEPACK_H */
