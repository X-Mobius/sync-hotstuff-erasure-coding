#ifndef CODE_FUNCTION_H
#define CODE_FUNCTION_H

#ifdef __cplusplus
extern "C" {
#endif

void encode_function(int m, int k, unsigned char **buffs);

void decode_function(int m, int k, unsigned char *buffs[],
                     unsigned char src_in_err[], unsigned char src_err_list[],
                     int nerrs, int nsrcerrs, unsigned char *temp_buffs[]);

/* Encode k data shards into n total shards. All buffers must be len bytes. */
int rs_encode_shards(int n, int k, int len, unsigned char **shards);

/* Reconstruct missing data shards in-place from any k present shards. */
int rs_reconstruct_data(int n, int k, int len, unsigned char **shards,
                        const unsigned char *present);


#ifdef __cplusplus
}
#endif

#endif // CODE_FUNCTION_H
