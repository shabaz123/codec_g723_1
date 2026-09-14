#ifndef G723_BACKEND_H
#define G723_BACKEND_H

#include "codec_g723_1.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct g723_backend_encoder g723_backend_encoder_t;
typedef struct g723_backend_decoder g723_backend_decoder_t;

typedef struct {
    uint32_t abi_version;
    const char *name;

    int (*supports_bitrate)(
        g723_bitrate_t bitrate
    );

    g723_result_t (*encoder_create)(
        g723_bitrate_t bitrate,
        g723_backend_encoder_t **encoder
    );

    void (*encoder_destroy)(
        g723_backend_encoder_t *encoder
    );

    g723_result_t (*encode_frame)(
        g723_backend_encoder_t *encoder,
        const int16_t pcm[G723_PCM_SAMPLES_PER_FRAME],
        uint8_t *output,
        size_t output_capacity,
        size_t *output_size
    );

    g723_result_t (*decoder_create)(
        g723_backend_decoder_t **decoder
    );

    void (*decoder_destroy)(
        g723_backend_decoder_t *decoder
    );

    g723_result_t (*decode_frame)(
        g723_backend_decoder_t *decoder,
        const uint8_t *input,
        size_t input_size,
        int16_t pcm[G723_PCM_SAMPLES_PER_FRAME]
    );

    g723_frame_type_t (*frame_type)(
        const uint8_t *input,
        size_t input_size
    );

    size_t (*frame_size)(
        g723_frame_type_t type
    );

} g723_backend_v1_t;

const g723_backend_v1_t *g723_get_compiled_backend(void);

#ifdef __cplusplus
}
#endif

#endif
