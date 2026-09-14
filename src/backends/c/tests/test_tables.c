#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../g723_tables.h"

int main(void) {
    printf("Testing specification tables...\n");

    // 1. LSP DC frequencies strictly increasing
    for (int i = 1; i < 10; i++) {
        assert(G723_LSP_DC_PREDICTED_FREQ_Q15[i] > G723_LSP_DC_PREDICTED_FREQ_Q15[i - 1]);
    }

    // 2. LSP codebook row 0 is all zeroes
    for (size_t b = 0; b < 3; b++) {
        size_t dim = 0;
        const int16_t *row0 = g723_lsp_codebook_entry(b, 0, &dim);
        assert(row0 != NULL);
        assert(dim == (b == 2 ? 4 : 3));
        for (size_t j = 0; j < dim; j++) {
            assert(row0[j] == 0);
        }
    }

    // 3. Pascal recurrence on MPMLQ_COMBINATORIAL
    for (size_t r = 0; r < 5; r++) {
        for (size_t c = 0; c < 29; c++) {
            uint32_t left = G723_MPMLQ_COMBINATORIAL[r * 30 + c];
            uint32_t succ_right = G723_MPMLQ_COMBINATORIAL[r * 30 + c + 1];
            uint32_t succ_diag = G723_MPMLQ_COMBINATORIAL[(r + 1) * 30 + c + 1];
            if (left == 0 || (succ_right == 0 && succ_diag == 0)) {
                continue;
            }
            assert(left == succ_right + succ_diag);
        }
    }

    // 4. ACELP tracks match Table 1
    const int expected[4][8] = {
        {0, 8, 16, 24, 32, 40, 48, 56},
        {2, 10, 18, 26, 34, 42, 50, 58},
        {4, 12, 20, 28, 36, 44, 52, -1},
        {6, 14, 22, 30, 38, 46, 54, -1}
    };
    for (size_t t = 0; t < 4; t++) {
        for (size_t k = 0; k < 8; k++) {
            size_t pos = 0;
            bool valid = g723_acelp_track_position(t, k, false, &pos);
            if (expected[t][k] == -1) {
                assert(!valid);
            } else {
                assert(valid);
                assert((int)pos == expected[t][k]);
            }
        }
    }

    // 5. Combinatorial position codec exhaustive test for M=5 (all 142506 combinations)
    printf("Verifying M=5 combinatorial position codec (142506 combinations)...\n");
    uint32_t expected_idx = 0;
    for (size_t a = 0; a < 30; a++) {
        for (size_t b = a + 1; b < 30; b++) {
            for (size_t c = b + 1; c < 30; c++) {
                for (size_t d = c + 1; d < 30; d++) {
                    for (size_t e = d + 1; e < 30; e++) {
                        size_t pos[5] = {a, b, c, d, e};
                        uint32_t packed = 0;
                        assert(g723_fcbk_pack_positions(pos, 5, &packed));
                        assert(packed == expected_idx);

                        size_t unpacked[6] = {0};
                        assert(g723_fcbk_unpk_positions(packed, 5, unpacked));
                        assert(unpacked[0] == a && unpacked[1] == b && unpacked[2] == c &&
                               unpacked[3] == d && unpacked[4] == e);

                        expected_idx++;
                    }
                }
            }
        }
    }
    assert(expected_idx == G723_MPMLQ_MAX_POSITION[1]);

    // Test sample of M=6 (593775 combinations)
    printf("Verifying M=6 combinatorial position codec sample...\n");
    for (uint32_t idx = 0; idx < G723_MPMLQ_MAX_POSITION[0]; idx += 137) {
        size_t unp[6] = {0};
        assert(g723_fcbk_unpk_positions(idx, 6, unp));
        for (int i = 0; i < 5; i++) {
            assert(unp[i] < unp[i + 1]);
        }
        assert(unp[5] < 30);
        uint32_t repacked = 0;
        assert(g723_fcbk_pack_positions(unp, 6, &repacked));
        assert(repacked == idx);
    }

    printf("All specification table tests passed!\n");
    return 0;
}
