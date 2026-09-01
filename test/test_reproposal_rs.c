#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_function.h"

static int run_case(int n, int k, int len, const int *available, int navailable)
{
    unsigned char **shards = calloc((size_t)n, sizeof(*shards));
    unsigned char **original = calloc((size_t)k, sizeof(*original));
    unsigned char *present = calloc((size_t)n, 1);
    int i, j, rc = 1;
    if (!shards || !original || !present) goto out;
    for (i = 0; i < n; ++i) {
        if (posix_memalign((void **)&shards[i], 64, (size_t)len)) goto out;
        memset(shards[i], 0, (size_t)len);
    }
    for (i = 0; i < k; ++i) {
        original[i] = malloc((size_t)len);
        if (!original[i]) goto out;
        for (j = 0; j < len; ++j)
            shards[i][j] = (unsigned char)((i * 37 + j * 13 + 11) & 0xff);
        memcpy(original[i], shards[i], (size_t)len);
    }
    if (rs_encode_shards(n, k, len, shards) != 0) goto out;
    for (i = 0; i < navailable; ++i) present[available[i]] = 1;
    for (i = 0; i < n; ++i)
        if (!present[i]) memset(shards[i], 0, (size_t)len);
    if (rs_reconstruct_data(n, k, len, shards, present) != 0) goto out;
    for (i = 0; i < k; ++i)
        if (memcmp(shards[i], original[i], (size_t)len) != 0) goto out;
    rc = 0;
out:
    if (shards) {
        for (i = 0; i < n; ++i) free(shards[i]);
    }
    if (original) {
        for (i = 0; i < k; ++i) free(original[i]);
    }
    free(shards);
    free(original);
    free(present);
    return rc;
}

int main(void)
{
    const int f2_data_and_parity[] = {1, 3, 6};
    const int f2_all_parity[] = {3, 4, 5};
    const int f3_data_and_parity[] = {1, 4, 5, 6};
    if (run_case(7, 3, 32, f2_data_and_parity, 3) ||
        run_case(7, 3, 96, f2_all_parity, 3) ||
        run_case(7, 4, 32, f3_data_and_parity, 4) ||
        run_case(7, 4, 8192, f3_data_and_parity, 4)) {
        fprintf(stderr, "Reed-Solomon shard reconstruction test failed\n");
        return 1;
    }
    puts("Reed-Solomon shard reconstruction tests passed");
    return 0;
}
