#include "g723_linepack.h"
#include <string.h>

static const uint32_t HIGH_POS_LSB_BITS[G723_SUBFRAMES_PER_FRAME] = {16, 14, 16, 14};
static const uint32_t MSBPOS_RADIX[G723_SUBFRAMES_PER_FRAME] = {10, 9, 10, 9};
static const uint32_t ACL_BITS[G723_SUBFRAMES_PER_FRAME] = {7, 2, 7, 2};
static const uint32_t GAIN_BITS = 12;
static const uint32_t HIGH_PSIG_BITS[G723_SUBFRAMES_PER_FRAME] = {6, 5, 6, 5};
static const uint32_t LOW_POS_BITS = 12;
static const uint32_t LOW_PSIG_BITS = 4;
static const uint32_t MPMLQ_MAX_POSITION[G723_SUBFRAMES_PER_FRAME] = {593775, 142506, 593775, 142506};

uint32_t g723_msbpos_combine(const uint32_t pos[G723_SUBFRAMES_PER_FRAME]) {
    uint32_t w = 0;
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        uint32_t digit = pos[s] >> HIGH_POS_LSB_BITS[s];
        w = w * MSBPOS_RADIX[s] + digit;
    }
    return w;
}

bool g723_msbpos_split(uint32_t word, uint32_t digits[G723_SUBFRAMES_PER_FRAME]) {
    if (word >= G723_MSBPOS_LIMIT) {
        return false;
    }
    uint32_t w = word;
    for (int s = (int)G723_SUBFRAMES_PER_FRAME - 1; s >= 0; s--) {
        digits[s] = w % MSBPOS_RADIX[s];
        w /= MSBPOS_RADIX[s];
    }
    return true;
}

bool g723_validate_frame_params(const g723_frame_params_t *params) {
    if (params == NULL) {
        return false;
    }
    if ((params->lsp_index >> 24) != 0) {
        return false;
    }
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        if ((params->acl[s] >> ACL_BITS[s]) != 0) {
            return false;
        }
        if ((params->gain[s] >> GAIN_BITS) != 0) {
            return false;
        }
        if (params->grid[s] > 1) {
            return false;
        }
        if (params->rate == G723_RATE_HIGH) {
            if (params->pos[s] >= MPMLQ_MAX_POSITION[s]) {
                return false;
            }
            if ((params->psig[s] >> HIGH_PSIG_BITS[s]) != 0) {
                return false;
            }
        } else if (params->rate == G723_RATE_LOW) {
            if ((params->pos[s] >> LOW_POS_BITS) != 0) {
                return false;
            }
            if ((params->psig[s] >> LOW_PSIG_BITS) != 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

typedef struct {
    uint8_t *bytes;
    size_t capacity;
    size_t bit_pos;
} bit_writer_t;

static void bw_write(bit_writer_t *bw, uint32_t val, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bit = (val >> i) & 1u;
        size_t byte_idx = bw->bit_pos / 8;
        size_t bit_off = bw->bit_pos % 8;
        bw->bytes[byte_idx] |= (uint8_t)(bit << bit_off);
        bw->bit_pos++;
    }
}

typedef struct {
    const uint8_t *bytes;
    size_t total_bits;
    size_t bit_pos;
} bit_reader_t;

static bool br_read(bit_reader_t *br, uint32_t n, uint32_t *out) {
    if (n == 0) {
        *out = 0;
        return true;
    }
    if (br->total_bits - br->bit_pos < n) {
        return false;
    }
    uint32_t val = 0;
    uint32_t produced = 0;
    while (produced < n) {
        size_t byte_idx = br->bit_pos / 8;
        uint32_t bit_off = (uint32_t)(br->bit_pos % 8);
        uint32_t byte_val = br->bytes[byte_idx];
        uint32_t take = (8 - bit_off) < (n - produced) ? (8 - bit_off) : (n - produced);
        uint32_t chunk = (byte_val >> bit_off) & ((1u << take) - 1u);
        val |= (chunk << produced);
        produced += take;
        br->bit_pos += take;
    }
    *out = val;
    return true;
}

g723_result_t g723_pack_frame(
    const g723_frame_params_t *params,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size
) {
    if (params == NULL || output == NULL || output_size == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }
    *output_size = 0;
    if (!g723_validate_frame_params(params)) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    size_t needed_bytes = (params->rate == G723_RATE_HIGH) ? G723_HIGH_RATE_BYTES : G723_LOW_RATE_BYTES;
    if (output_capacity < needed_bytes) {
        return G723_RESULT_INVALID_ARGUMENT;
    }

    memset(output, 0, needed_bytes);
    bit_writer_t bw = { output, needed_bytes, 0 };

    uint32_t rateflag = (params->rate == G723_RATE_HIGH) ? 0u : 1u;
    bw_write(&bw, rateflag, 1);
    bw_write(&bw, 0, 1); /* VADFLAG = 0 (active speech) */
    bw_write(&bw, params->lsp_index, 24);

    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        bw_write(&bw, params->acl[s], ACL_BITS[s]);
    }
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        bw_write(&bw, params->gain[s], GAIN_BITS);
    }
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        bw_write(&bw, params->grid[s] & 1u, 1);
    }

    if (params->rate == G723_RATE_HIGH) {
        bw_write(&bw, 0, 1); /* UB = 0 */
        uint32_t msbpos = g723_msbpos_combine(params->pos);
        bw_write(&bw, msbpos, 13);

        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            uint32_t mask = (1u << HIGH_POS_LSB_BITS[s]) - 1u;
            bw_write(&bw, params->pos[s] & mask, HIGH_POS_LSB_BITS[s]);
        }
        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            bw_write(&bw, params->psig[s], HIGH_PSIG_BITS[s]);
        }
    } else {
        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            bw_write(&bw, params->pos[s], LOW_POS_BITS);
        }
        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            bw_write(&bw, params->psig[s], LOW_PSIG_BITS);
        }
    }

    *output_size = needed_bytes;
    return G723_RESULT_OK;
}

g723_result_t g723_unpack_frame(
    const uint8_t *data,
    size_t size,
    g723_frame_params_t *params
) {
    if (data == NULL || size == 0 || params == NULL) {
        return G723_RESULT_INVALID_ARGUMENT;
    }
    bit_reader_t br = { data, size * 8, 0 };

    uint32_t rateflag = 0;
    uint32_t vadflag = 0;
    if (!br_read(&br, 1, &rateflag) || !br_read(&br, 1, &vadflag)) {
        return G723_RESULT_BAD_FRAME;
    }
    if (vadflag != 0) {
        return G723_RESULT_BAD_FRAME; /* SID / reserved, not normal speech */
    }

    g723_rate_t rate = (rateflag == 0) ? G723_RATE_HIGH : G723_RATE_LOW;
    size_t expected_bytes = (rate == G723_RATE_HIGH) ? G723_HIGH_RATE_BYTES : G723_LOW_RATE_BYTES;
    if (size < expected_bytes) {
        return G723_RESULT_BAD_FRAME;
    }

    params->rate = rate;
    if (!br_read(&br, 24, &params->lsp_index)) {
        return G723_RESULT_BAD_FRAME;
    }

    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        if (!br_read(&br, ACL_BITS[s], &params->acl[s])) {
            return G723_RESULT_BAD_FRAME;
        }
    }
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        if (!br_read(&br, GAIN_BITS, &params->gain[s])) {
            return G723_RESULT_BAD_FRAME;
        }
    }
    for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
        uint32_t g = 0;
        if (!br_read(&br, 1, &g)) {
            return G723_RESULT_BAD_FRAME;
        }
        params->grid[s] = (uint8_t)g;
    }

    if (rate == G723_RATE_HIGH) {
        uint32_t ub = 0;
        uint32_t msbpos = 0;
        if (!br_read(&br, 1, &ub) || !br_read(&br, 13, &msbpos)) {
            return G723_RESULT_BAD_FRAME;
        }
        uint32_t digits[G723_SUBFRAMES_PER_FRAME];
        if (!g723_msbpos_split(msbpos, digits)) {
            return G723_RESULT_BAD_FRAME;
        }
        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            uint32_t lsbs = 0;
            if (!br_read(&br, HIGH_POS_LSB_BITS[s], &lsbs)) {
                return G723_RESULT_BAD_FRAME;
            }
            uint32_t code = (digits[s] << HIGH_POS_LSB_BITS[s]) | lsbs;
            if (code >= MPMLQ_MAX_POSITION[s]) {
                return G723_RESULT_BAD_FRAME;
            }
            params->pos[s] = code;
        }
        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            if (!br_read(&br, HIGH_PSIG_BITS[s], &params->psig[s])) {
                return G723_RESULT_BAD_FRAME;
            }
        }
    } else {
        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            if (!br_read(&br, LOW_POS_BITS, &params->pos[s])) {
                return G723_RESULT_BAD_FRAME;
            }
        }
        for (size_t s = 0; s < G723_SUBFRAMES_PER_FRAME; s++) {
            if (!br_read(&br, LOW_PSIG_BITS, &params->psig[s])) {
                return G723_RESULT_BAD_FRAME;
            }
        }
    }

    return G723_RESULT_OK;
}
