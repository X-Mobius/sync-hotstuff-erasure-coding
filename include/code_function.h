#ifndef CODE_FUNCTION_H
#define CODE_FUNCTION_H

#ifdef __cplusplus
extern "C" {
#endif

void encode_function(int m, int k, unsigned char **buffs);

void decode_function(int m, int k, unsigned char *buffs[],
                     unsigned char src_in_err[], unsigned char src_err_list[],
                     int nerrs, int nsrcerrs, unsigned char *temp_buffs[]);


#ifdef __cplusplus
}
#endif

#endif // CODE_FUNCTION_H
