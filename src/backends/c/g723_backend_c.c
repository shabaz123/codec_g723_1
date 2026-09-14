#include "g723_backend.h"
#include "g723_encoder.h"
#include "g723_qdec.h"
#include "g723_header.h"
#include "g723_resample.h"

#include <stdlib.h>
#include <string.h>

struct g723_backend_encoder {
    g723_encoder_state_t enc;
    g723_resampler_state_t resampler;
};

struct g723_backend_decoder {
    g723_qdec_state_t dec;
    g723_resampler_state_t resampler;
};

static int c_supports_bitrate(g723_bitrate_t bitrate) {
    return (bitrate == G723_BITRATE_5300 || bitrate == G723_BITRATE_6300) ? 1 : 0;
}

static g723_result_t c_encoder_create(
    g723_bitrate_t bitrate,
    g723_backend_encoder_t **encoder_out
) {
    if (encoder_out == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }
    *encoder_out = NULL;

    if (!c_supports_bitrate(bitrate)) {
        return G723_RESULT_UNSUPPORTED;
    }

    g723_backend_encoder_t *enc = (g723_backend_encoder_t *)malloc(sizeof(g723_backend_encoder_t));
    if (enc == NULL) {
        return G723_RESULT_NO_MEMORY;
    }

    g723_rate_t rate = (bitrate == G723_BITRATE_6300) ? G723_RATE_HIGH : G723_RATE_LOW;
    g723_encoder_init(&enc->enc, rate);
    g723_resampler_init(&enc->resampler);

    *encoder_out = enc;
    return G723_RESULT_OK;
}

static void c_encoder_destroy(g723_backend_encoder_t *encoder) {
    if (encoder != NULL) {
        free(encoder);
    }
}

static g723_result_t c_encode_frame(
    g723_backend_encoder_t *encoder,
    const int16_t pcm[G723_PCM_SAMPLES_PER_FRAME],
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size
) {
    if (encoder == NULL || pcm == NULL || output == NULL || output_size == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    *output_size = output_capacity;
    return g723_encoder_encode_frame(&encoder->enc, pcm, output, output_size);
}

static g723_result_t c_decoder_create(g723_backend_decoder_t **decoder_out) {
    if (decoder_out == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }
    *decoder_out = NULL;

    g723_backend_decoder_t *dec = (g723_backend_decoder_t *)malloc(sizeof(g723_backend_decoder_t));
    if (dec == NULL) {
        return G723_RESULT_NO_MEMORY;
    }

    g723_qdec_init(&dec->dec);
    g723_resampler_init(&dec->resampler);

    *decoder_out = dec;
    return G723_RESULT_OK;
}

static void c_decoder_destroy(g723_backend_decoder_t *decoder) {
    if (decoder != NULL) {
        free(decoder);
    }
}

static g723_result_t c_decode_frame(
    g723_backend_decoder_t *decoder,
    const uint8_t *input,
    size_t input_size,
    int16_t pcm[G723_PCM_SAMPLES_PER_FRAME]
) {
    if (decoder == NULL || input == NULL || pcm == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    return g723_qdec_decode_frame(&decoder->dec, input, input_size, pcm);
}

static g723_frame_type_t c_frame_type(const uint8_t *input, size_t input_size) {
    return g723_parse_frame_type(input, input_size);
}

static size_t c_frame_size(g723_frame_type_t type) {
    return g723_expected_frame_size(type);
}

static const g723_backend_v1_t c_backend = {
    .abi_version = G723_ABI_VERSION,
    .name = "c",
    .supports_bitrate = c_supports_bitrate,
    .encoder_create = c_encoder_create,
    .encoder_destroy = c_encoder_destroy,
    .encode_frame = c_encode_frame,
    .decoder_create = c_decoder_create,
    .decoder_destroy = c_decoder_destroy,
    .decode_frame = c_decode_frame,
    .frame_type = c_frame_type,
    .frame_size = c_frame_size,
};

const g723_backend_v1_t *g723_get_compiled_backend(void) {
    return &c_backend;
}
