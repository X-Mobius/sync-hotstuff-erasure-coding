#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#define TEST_SOURCES 127
#define TEST_LEN 8192

int main() 
{
    int i, m, k, j;
    void *buf;
    unsigned char *buffs[TEST_SOURCES] = { NULL };
    srand(time(NULL));

    // Allocate the arrays: Make 127 arrays 64-byte aligned
    for (i = 0; i < TEST_SOURCES; i++) 
    {                      
        if (posix_memalign(&buf, 64, TEST_LEN)) 
        {
            printf("alloc error: Fail\n");
            for (j = 0; j < i; j++) // Correct loop range for cleanup
            {
                if (buffs[j])
                    free(buffs[j]);
            }
            return 1;
        }
        buffs[i] = buf;
    }

    m = 3;
    k = 2;
    // Initialize the first k rows with random data
    for (i = 0; i < k; i++) 
    {
        for (j = 0; j < TEST_LEN; j++) 
        {
            buffs[i][j] = rand() % 256;
        }
    }

    encode_function(m, k, buffs);

    int nerrs, nsrcerrs;
    unsigned char *temp_buffs[TEST_SOURCES] = { NULL };
    unsigned char src_in_err[TEST_SOURCES] = {0}; 
    unsigned char src_err_list[TEST_SOURCES] = {0}; 
    FILE *input_file; // Note: This variable is declared but unused

    // Allocate arrays for temporary buffers
    for (i = 0; i < TEST_SOURCES; i++) 
    {
        if (posix_memalign(&buf, 64, TEST_LEN)) 
        {
            printf("alloc error: Fail\n");
            for (j = 0; j < i; j++) // Correct loop range for cleanup
            {
                if (temp_buffs[j])
                    free(temp_buffs[j]);
            }
            // Also free buffs to avoid memory leaks
            for (j = 0; j < TEST_SOURCES; j++) 
            {
                if (buffs[j])
                    free(buffs[j]);
            }
            return 1;
        }
        temp_buffs[i] = buf;
    }

    nerrs = 1;//总块的错误
	nsrcerrs = 1;//数据块的错误
    src_in_err[1] = 1;//第二行出错
	src_err_list[0] = 1;//索引出错块

    decode_function(m, k, buffs, src_in_err, src_err_list, nerrs, nsrcerrs, temp_buffs);

    for (i = 0; i < nerrs; i++) 
    {
        if (0 != memcmp(temp_buffs[k + i], buffs[src_err_list[i]], TEST_LEN)) 
        {
            printf("Fail error recovery (%d, %d, %d)\n", m, k, nerrs);
        } 
        else 
        {
            printf("Success!\n");
        }
    }

    // Free memory for buffs and temp_buffs
    for (i = 0; i < TEST_SOURCES; i++) 
    {
        if (buffs[i])
            free(buffs[i]);
    }

    for (i = 0; i < TEST_SOURCES; i++) 
    {
        if (temp_buffs[i])
            free(temp_buffs[i]);
    }

    return 0;
}
