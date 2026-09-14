#ifndef G723_HEADER_H
#define G723_HEADER_H

#include <stdint.h>
#include <stddef.h>
#include "codec_g723_1.h"

#ifdef __cplusplus
extern "C" {
#endif

g723_frame_type_t g723_parse_frame_type(const uint8_t *data, size_t size);
size_t g723_expected_frame_size(g723_frame_type_t type);
const char *g723_frame_type_name(g723_frame_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* G723_HEADER_H */
