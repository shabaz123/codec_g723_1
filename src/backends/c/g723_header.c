#include "g723_header.h"
#include "g723_consts.h"

g723_frame_type_t g723_parse_frame_type(const uint8_t *data, size_t size) {
    if (data == NULL || size == 0) {
        return G723_FRAME_TYPE_INVALID;
    }
    switch (data[0] & 0x03u) {
        case 0:
            return G723_FRAME_TYPE_6300;
        case 1:
            return G723_FRAME_TYPE_5300;
        case 2:
            return G723_FRAME_TYPE_SID;
        case 3:
            return G723_FRAME_TYPE_UNTRANSMITTED;
        default:
            return G723_FRAME_TYPE_INVALID;
    }
}

size_t g723_expected_frame_size(g723_frame_type_t type) {
    switch (type) {
        case G723_FRAME_TYPE_6300:
            return G723_HIGH_RATE_BYTES;
        case G723_FRAME_TYPE_5300:
            return G723_LOW_RATE_BYTES;
        case G723_FRAME_TYPE_SID:
            return G723_SID_BYTES;
        case G723_FRAME_TYPE_UNTRANSMITTED:
            return G723_UNTRANSMITTED_BYTES;
        case G723_FRAME_TYPE_INVALID:
        default:
            return 0;
    }
}

const char *g723_frame_type_name(g723_frame_type_t type) {
    switch (type) {
        case G723_FRAME_TYPE_6300:
            return "6.3 kbit/s";
        case G723_FRAME_TYPE_5300:
            return "5.3 kbit/s";
        case G723_FRAME_TYPE_SID:
            return "SID (comfort-noise)";
        case G723_FRAME_TYPE_UNTRANSMITTED:
            return "untransmitted";
        default:
            return "invalid";
    }
}
