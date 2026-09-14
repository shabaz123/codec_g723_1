#include "codec_g723_1.h"
#include "g723_backend.h"
#include "backends/c/g723_resample.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    const g723_backend_v1_t *backend;
    g723_backend_encoder_t *backend_encoder;
    uint32_t sample_rate;
    g723_resampler_state_t resampler;
} g723_encoder_instance_t;

typedef struct {
    const g723_backend_v1_t *backend;
    g723_backend_decoder_t *backend_decoder;
    uint32_t sample_rate;
    g723_resampler_state_t resampler;
} g723_decoder_instance_t;

static const g723_backend_v1_t *get_backend(void) {
    const g723_backend_v1_t *backend =
        g723_get_compiled_backend();

    if (backend == NULL) {
        return NULL;
    }

    if (backend->abi_version != G723_ABI_VERSION) {
        return NULL;
    }

    return backend;
}

uint32_t g723_abi_version(void) {
    return G723_ABI_VERSION;
}

const char *g723_backend_name(void) {
    const g723_backend_v1_t *backend = get_backend();

    if (backend == NULL || backend->name == NULL) {
        return "unknown";
    }

    return backend->name;
}

int32_t g723_backend_supports_bitrate(int32_t bitrate) {
    const g723_backend_v1_t *backend = get_backend();

    if (backend == NULL ||
        backend->supports_bitrate == NULL) {
        return 0;
    }

    if (bitrate != G723_BITRATE_5300 &&
        bitrate != G723_BITRATE_6300) {
        return 0;
    }

    return backend->supports_bitrate(
        (g723_bitrate_t)bitrate
    ) ? 1 : 0;
}

int32_t g723_encoder_create(
    int32_t bitrate,
    void **encoder_out
) {
    const g723_backend_v1_t *backend;
    g723_backend_encoder_t *backend_encoder = NULL;
    g723_encoder_instance_t *instance;
    g723_result_t result;

    if (encoder_out == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    *encoder_out = NULL;

    if (bitrate != G723_BITRATE_5300 &&
        bitrate != G723_BITRATE_6300) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    backend = get_backend();

    if (backend == NULL ||
        backend->encoder_create == NULL ||
        backend->encoder_destroy == NULL) {
        return G723_RESULT_INTERNAL;
    }

    if (backend->supports_bitrate == NULL ||
        !backend->supports_bitrate(
            (g723_bitrate_t)bitrate
        )) {
        return G723_RESULT_UNSUPPORTED;
    }

    result = backend->encoder_create(
        (g723_bitrate_t)bitrate,
        &backend_encoder
    );

    if (result != G723_RESULT_OK) {
        return result;
    }

    if (backend_encoder == NULL) {
        return G723_RESULT_INTERNAL;
    }

    instance = (g723_encoder_instance_t *)
        malloc(sizeof(g723_encoder_instance_t));

    if (instance == NULL) {
        backend->encoder_destroy(backend_encoder);
        return G723_RESULT_NO_MEMORY;
    }

    instance->backend = backend;
    instance->backend_encoder = backend_encoder;
    instance->sample_rate = G723_PCM_SAMPLE_RATE;
    g723_resampler_init(&instance->resampler);

    *encoder_out = instance;

    return G723_RESULT_OK;
}

int32_t g723_encoder_set_sample_rate(void *encoder, uint32_t sample_rate) {
    if (encoder == NULL) return G723_RESULT_INVALID_ARGUMENT;
    if (sample_rate != G723_PCM_SAMPLE_RATE && sample_rate != G723_PCM_SAMPLE_RATE_16K) {
        return G723_RESULT_INVALID_ARGUMENT;
    }
    g723_encoder_instance_t *instance = (g723_encoder_instance_t *)encoder;
    instance->sample_rate = sample_rate;
    return G723_RESULT_OK;
}

void g723_encoder_destroy(void *encoder) {
    g723_encoder_instance_t *instance;

    if (encoder == NULL) {
        return;
    }

    instance = (g723_encoder_instance_t *)encoder;

    if (instance->backend != NULL &&
        instance->backend->encoder_destroy != NULL &&
        instance->backend_encoder != NULL) {
        instance->backend->encoder_destroy(
            instance->backend_encoder
        );
    }

    free(instance);
}

int32_t g723_encode_frame(
    void *encoder,
    const int16_t *pcm,
    uint32_t pcm_samples,
    uint8_t *output,
    uint32_t output_capacity,
    uint32_t *output_size
) {
    g723_encoder_instance_t *instance;
    size_t backend_output_size = 0;
    g723_result_t result;

    if (output_size != NULL) {
        *output_size = 0;
    }

    if (encoder == NULL ||
        pcm == NULL ||
        output == NULL ||
        output_size == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    if (pcm_samples != G723_PCM_SAMPLES_PER_FRAME &&
        pcm_samples != G723_PCM_SAMPLES_PER_FRAME_16K) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    if (output_capacity < G723_FRAME_BYTES_6300) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    instance = (g723_encoder_instance_t *)encoder;

    if (instance->backend == NULL ||
        instance->backend->encode_frame == NULL ||
        instance->backend_encoder == NULL) {
        return G723_RESULT_INTERNAL;
    }

    const int16_t *pcm_to_encode = pcm;
    int16_t pcm_8k[G723_PCM_SAMPLES_PER_FRAME];
    if (pcm_samples == G723_PCM_SAMPLES_PER_FRAME_16K) {
        g723_resample_16k_to_8k(&instance->resampler, pcm, pcm_8k);
        pcm_to_encode = pcm_8k;
    }

    result = instance->backend->encode_frame(
        instance->backend_encoder,
        pcm_to_encode,
        output,
        (size_t)output_capacity,
        &backend_output_size
    );

    if (result != G723_RESULT_OK) {
        return result;
    }

    if (backend_output_size > output_capacity ||
        backend_output_size > UINT32_MAX) {
        return G723_RESULT_INTERNAL;
    }

    *output_size = (uint32_t)backend_output_size;

    return G723_RESULT_OK;
}

int32_t g723_decoder_create(
    void **decoder_out
) {
    const g723_backend_v1_t *backend;
    g723_backend_decoder_t *backend_decoder = NULL;
    g723_decoder_instance_t *instance;
    g723_result_t result;

    if (decoder_out == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    *decoder_out = NULL;

    backend = get_backend();

    if (backend == NULL ||
        backend->decoder_create == NULL ||
        backend->decoder_destroy == NULL) {
        return G723_RESULT_INTERNAL;
    }

    result = backend->decoder_create(
        &backend_decoder
    );

    if (result != G723_RESULT_OK) {
        return result;
    }

    if (backend_decoder == NULL) {
        return G723_RESULT_INTERNAL;
    }

    instance = (g723_decoder_instance_t *)
        malloc(sizeof(g723_decoder_instance_t));

    if (instance == NULL) {
        backend->decoder_destroy(backend_decoder);
        return G723_RESULT_NO_MEMORY;
    }

    instance->backend = backend;
    instance->backend_decoder = backend_decoder;
    instance->sample_rate = G723_PCM_SAMPLE_RATE;
    g723_resampler_init(&instance->resampler);

    *decoder_out = instance;

    return G723_RESULT_OK;
}

int32_t g723_decoder_set_sample_rate(void *decoder, uint32_t sample_rate) {
    if (decoder == NULL) return G723_RESULT_INVALID_ARGUMENT;
    if (sample_rate != G723_PCM_SAMPLE_RATE && sample_rate != G723_PCM_SAMPLE_RATE_16K) {
        return G723_RESULT_INVALID_ARGUMENT;
    }
    g723_decoder_instance_t *instance = (g723_decoder_instance_t *)decoder;
    instance->sample_rate = sample_rate;
    return G723_RESULT_OK;
}

void g723_decoder_destroy(void *decoder) {
    g723_decoder_instance_t *instance;

    if (decoder == NULL) {
        return;
    }

    instance = (g723_decoder_instance_t *)decoder;

    if (instance->backend != NULL &&
        instance->backend->decoder_destroy != NULL &&
        instance->backend_decoder != NULL) {
        instance->backend->decoder_destroy(
            instance->backend_decoder
        );
    }

    free(instance);
}

int32_t g723_decode_frame(
    void *decoder,
    const uint8_t *input,
    uint32_t input_size,
    int16_t *pcm,
    uint32_t pcm_capacity,
    uint32_t *pcm_samples
) {
    g723_decoder_instance_t *instance;
    g723_result_t result;

    if (pcm_samples != NULL) {
        *pcm_samples = 0;
    }

    if (decoder == NULL ||
        input == NULL ||
        input_size == 0 ||
        pcm == NULL ||
        pcm_samples == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    instance = (g723_decoder_instance_t *)decoder;

    if (instance->backend == NULL ||
        instance->backend->decode_frame == NULL ||
        instance->backend_decoder == NULL) {
        return G723_RESULT_INTERNAL;
    }

    if (instance->sample_rate == G723_PCM_SAMPLE_RATE_16K) {
        if (pcm_capacity < G723_PCM_SAMPLES_PER_FRAME_16K) {
            return G723_RESULT_INVALID_ARGUMENT;
        }

        int16_t pcm_8k[G723_PCM_SAMPLES_PER_FRAME];
        result = instance->backend->decode_frame(
            instance->backend_decoder,
            input,
            (size_t)input_size,
            pcm_8k
        );

        if (result != G723_RESULT_OK) {
            return result;
        }

        g723_resample_8k_to_16k(&instance->resampler, pcm_8k, pcm);
        *pcm_samples = G723_PCM_SAMPLES_PER_FRAME_16K;
    } else {
        if (pcm_capacity < G723_PCM_SAMPLES_PER_FRAME) {
            return G723_RESULT_INVALID_ARGUMENT;
        }

        result = instance->backend->decode_frame(
            instance->backend_decoder,
            input,
            (size_t)input_size,
            pcm
        );

        if (result != G723_RESULT_OK) {
            return result;
        }

        *pcm_samples = G723_PCM_SAMPLES_PER_FRAME;
    }

    return G723_RESULT_OK;
}

int32_t g723_frame_type(
    const uint8_t *input,
    uint32_t input_size
) {
    const g723_backend_v1_t *backend = get_backend();

    if (input == NULL || input_size == 0) {
        return G723_FRAME_TYPE_INVALID;
    }

    if (backend == NULL ||
        backend->frame_type == NULL) {
        return G723_FRAME_TYPE_INVALID;
    }

    return (int32_t)backend->frame_type(
        input,
        (size_t)input_size
    );
}

uint32_t g723_frame_size(
    int32_t frame_type
) {
    const g723_backend_v1_t *backend = get_backend();
    size_t size;

    if (backend == NULL ||
        backend->frame_size == NULL) {
        return 0;
    }

    switch (frame_type) {
        case G723_FRAME_TYPE_6300:
        case G723_FRAME_TYPE_5300:
        case G723_FRAME_TYPE_SID:
        case G723_FRAME_TYPE_UNTRANSMITTED:
            break;

        default:
            return 0;
    }

    size = backend->frame_size(
        (g723_frame_type_t)frame_type
    );

    if (size > UINT32_MAX) {
        return 0;
    }

    return (uint32_t)size;
}
