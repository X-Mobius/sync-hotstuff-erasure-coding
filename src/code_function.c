#include <stdio.h>
#include <stdlib.h>
#include <string.h>	
#include <assert.h>
#include "code_function.h"
#include "ec_base.h"		// for GF tables


#define TEST_LEN 8192
#define TEST_SIZE (TEST_LEN/2)

#ifndef TEST_SOURCES                                          /*防止宏的重复定义*/
# define TEST_SOURCES  127
#endif
#ifndef RANDOMS
# define RANDOMS 200
#endif

#define MMAX TEST_SOURCES
#define KMAX TEST_SOURCES

#define EFENCE_TEST_MIN_SIZE 16
#define EFENCE_TEST_MAX_SIZE EFENCE_TEST_MIN_SIZE + 0x100    /*272*/

#ifdef EC_ALIGNED_ADDR                                       /*这样的条件编译通常用于在不同的编译环境或配置中提供灵活性。例如，开发者可能希望在调试模式下进行更严格的对齐检查，而在发布版本中则不进行这些检查*/
// Define power of 2 range to check ptr, len alignment       /*用于检查指针（ptr）和长度（len）对齐的2的幂范围*/
# define PTR_ALIGN_CHK_B 0
# define LEN_ALIGN_CHK_B 0	// 0 for aligned only            /*只有当数据地址按照预期的边界对齐时，相关的逻辑才会执行*/
#else
// Define power of 2 range to check ptr, len alignment
# define PTR_ALIGN_CHK_B 32                                  /*指针和长度将在一个更宽松的对齐范围内进行检查，允许最多32字节的对齐*/
# define LEN_ALIGN_CHK_B 32	// 0 for aligned only
#endif

#ifndef TEST_SEED
#define TEST_SEED 11
#endif

typedef unsigned char u8;

unsigned char gf_mul(unsigned char a, unsigned char b)
{
#ifndef GF_LARGE_TABLES
	int i;

	if ((a == 0) || (b == 0))
		return 0;

	return gff_base[(i = gflog_base[a] + gflog_base[b]) > 254 ? i - 255 : i];
#else
	return gf_mul_table_base[b * 256 + a];
#endif
}

void gf_gen_rs_matrix(unsigned char *a, int m, int k)
{
	int i, j;
	unsigned char p, gen = 1;

	memset(a, 0, k * m);
	for (i = 0; i < k; i++)
		a[k * i + i] = 1;

	for (i = k; i < m; i++) {
		p = 1;
		for (j = 0; j < k; j++) {
			a[k * i + j] = p;
			p = gf_mul(p, gen);
		}
		gen = gf_mul(gen, 2);
	}
}

void gf_vect_mul_init(unsigned char c, unsigned char *tbl)
{
	unsigned char c2 = (c << 1) ^ ((c & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	unsigned char c4 = (c2 << 1) ^ ((c2 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	unsigned char c8 = (c4 << 1) ^ ((c4 & 0x80) ? 0x1d : 0);	//Mult by GF{2}

#if (__WORDSIZE == 64 || _WIN64 || __x86_64__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
	unsigned long long v1, v2, v4, v8, *t;
	unsigned long long v10, v20, v40, v80;
	unsigned char c17, c18, c20, c24;

	t = (unsigned long long *)tbl;

	v1 = c * 0x0100010001000100ull;
	v2 = c2 * 0x0101000001010000ull;
	v4 = c4 * 0x0101010100000000ull;
	v8 = c8 * 0x0101010101010101ull;

	v4 = v1 ^ v2 ^ v4;
	t[0] = v4;
	t[1] = v8 ^ v4;

	c17 = (c8 << 1) ^ ((c8 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	c18 = (c17 << 1) ^ ((c17 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	c20 = (c18 << 1) ^ ((c18 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	c24 = (c20 << 1) ^ ((c20 & 0x80) ? 0x1d : 0);	//Mult by GF{2}

	v10 = c17 * 0x0100010001000100ull;
	v20 = c18 * 0x0101000001010000ull;
	v40 = c20 * 0x0101010100000000ull;
	v80 = c24 * 0x0101010101010101ull;

	v40 = v10 ^ v20 ^ v40;
	t[2] = v40;
	t[3] = v80 ^ v40;

#else // 32-bit or other
	unsigned char c3, c5, c6, c7, c9, c10, c11, c12, c13, c14, c15;
	unsigned char c17, c18, c19, c20, c21, c22, c23, c24, c25, c26, c27, c28, c29, c30,
	    c31;

	c3 = c2 ^ c;
	c5 = c4 ^ c;
	c6 = c4 ^ c2;
	c7 = c4 ^ c3;

	c9 = c8 ^ c;
	c10 = c8 ^ c2;
	c11 = c8 ^ c3;
	c12 = c8 ^ c4;
	c13 = c8 ^ c5;
	c14 = c8 ^ c6;
	c15 = c8 ^ c7;

	tbl[0] = 0;
	tbl[1] = c;
	tbl[2] = c2;
	tbl[3] = c3;
	tbl[4] = c4;
	tbl[5] = c5;
	tbl[6] = c6;
	tbl[7] = c7;
	tbl[8] = c8;
	tbl[9] = c9;
	tbl[10] = c10;
	tbl[11] = c11;
	tbl[12] = c12;
	tbl[13] = c13;
	tbl[14] = c14;
	tbl[15] = c15;

	c17 = (c8 << 1) ^ ((c8 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	c18 = (c17 << 1) ^ ((c17 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	c19 = c18 ^ c17;
	c20 = (c18 << 1) ^ ((c18 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	c21 = c20 ^ c17;
	c22 = c20 ^ c18;
	c23 = c20 ^ c19;
	c24 = (c20 << 1) ^ ((c20 & 0x80) ? 0x1d : 0);	//Mult by GF{2}
	c25 = c24 ^ c17;
	c26 = c24 ^ c18;
	c27 = c24 ^ c19;
	c28 = c24 ^ c20;
	c29 = c24 ^ c21;
	c30 = c24 ^ c22;
	c31 = c24 ^ c23;

	tbl[16] = 0;
	tbl[17] = c17;
	tbl[18] = c18;
	tbl[19] = c19;
	tbl[20] = c20;
	tbl[21] = c21;
	tbl[22] = c22;
	tbl[23] = c23;
	tbl[24] = c24;
	tbl[25] = c25;
	tbl[26] = c26;
	tbl[27] = c27;
	tbl[28] = c28;
	tbl[29] = c29;
	tbl[30] = c30;
	tbl[31] = c31;

#endif //__WORDSIZE == 64 || _WIN64 || __x86_64__
}

void ec_init_tables_base(int k, int rows, unsigned char *a, unsigned char *g_tbls)    /*用于初始化用于快速 Erasure Code 编码和解码的表格*/
{
	int i, j;

	for (i = 0; i < rows; i++) {
		for (j = 0; j < k; j++) {
			gf_vect_mul_init(*a++, g_tbls);
			g_tbls += 32;
		}
	}
}

void ec_init_tables(int k, int rows, unsigned char *a, unsigned char *g_tbls)
{
	return ec_init_tables_base(k, rows, a, g_tbls);
}

void ec_encode_data_base(int len, int srcs, int dests, unsigned char *v,
			 unsigned char **src, unsigned char **dest)
{
	int i, j, l;
	unsigned char s;

	for (l = 0; l < dests; l++) {
		for (i = 0; i < len; i++) {
			s = 0;
			for (j = 0; j < srcs; j++)
				s ^= gf_mul(src[j][i], v[j * 32 + l * srcs * 32 + 1]);

			dest[l][i] = s;
		}
	}
}

void ec_encode_data(int len, int srcs, int dests, unsigned char *v,
		    unsigned char **src, unsigned char **dest)
{
	ec_encode_data_base(len, srcs, dests, v, src, dest);
}

void encode_function(int m,int k,unsigned char *buffs[TEST_SOURCES]) 
{
    int re = -1;
	int i, j, p, rtest;
	unsigned int decode_index[MMAX];
	unsigned char *temp_buffs[TEST_SOURCES] = { NULL };
	unsigned char *encode_matrix = NULL, *decode_matrix = NULL, *invert_matrix =
	    NULL, *g_tbls = NULL;
	unsigned char *recov[TEST_SOURCES];
	int rows, align, size;
	unsigned char *efence_buffs[TEST_SOURCES];
	unsigned int offset;
	u8 *ubuffs[TEST_SOURCES];
	u8 *temp_ubuffs[TEST_SOURCES];

    encode_matrix = malloc(MMAX * KMAX);                   /*动态分配矩阵内存*/
	decode_matrix = malloc(MMAX * KMAX);
	invert_matrix = malloc(MMAX * KMAX);
	g_tbls = malloc(KMAX * TEST_SOURCES * 32);
	if (encode_matrix == NULL || decode_matrix == NULL
	    || invert_matrix == NULL || g_tbls == NULL) {
		printf("Test failure! Error with malloc\n");
		goto exit;
	}

    assert((m <= MMAX) && (k <= KMAX));

    //Generate encode matrix encode_matrix
	// The matrix generated by gf_gen_rs_matrix
	// is not always invertable.
	gf_gen_rs_matrix(encode_matrix, m, k);

    // Generate g_tbls from encode matrix encode_matrix
	ec_init_tables(k, m - k, &encode_matrix[k * k], g_tbls);       /*将编码矩阵中的冗余数据提取出来，并初始化到编码表 g_tbls 中，以便在编码过程中进行乘法运算。*/

	// Perform matrix dot_prod for EC encoding
	// using g_tbls from encode matrix encode_matrix
	ec_encode_data(TEST_LEN, k, m - k, g_tbls, buffs, &buffs[k]);

	 exit:
	free(encode_matrix);
	free(decode_matrix);
	free(invert_matrix);
	free(g_tbls);

}
unsigned char gf_inv(unsigned char a)
{
#ifndef GF_LARGE_TABLES
	if (a == 0)
		return 0;

	return gff_base[255 - gflog_base[a]];
#else
	return gf_inv_table_base[a];
#endif
}

int gf_invert_matrix(unsigned char *in_mat, unsigned char *out_mat, const int n)
{
	int i, j, k;
	unsigned char temp;

	// Set out_mat[] to the identity matrix
	for (i = 0; i < n * n; i++)	// memset(out_mat, 0, n*n)
		out_mat[i] = 0;

	for (i = 0; i < n; i++)
		out_mat[i * n + i] = 1;

	// Inverse
	for (i = 0; i < n; i++) {
		// Check for 0 in pivot element
		if (in_mat[i * n + i] == 0) {
			// Find a row with non-zero in current column and swap
			for (j = i + 1; j < n; j++)
				if (in_mat[j * n + i])
					break;

			if (j == n)	// Couldn't find means it's singular
				return -1;

			for (k = 0; k < n; k++) {	// Swap rows i,j
				temp = in_mat[i * n + k];
				in_mat[i * n + k] = in_mat[j * n + k];
				in_mat[j * n + k] = temp;

				temp = out_mat[i * n + k];
				out_mat[i * n + k] = out_mat[j * n + k];
				out_mat[j * n + k] = temp;
			}
		}

		temp = gf_inv(in_mat[i * n + i]);	// 1/pivot
		for (j = 0; j < n; j++) {	// Scale row i by 1/pivot
			in_mat[i * n + j] = gf_mul(in_mat[i * n + j], temp);
			out_mat[i * n + j] = gf_mul(out_mat[i * n + j], temp);
		}

		for (j = 0; j < n; j++) {
			if (j == i)
				continue;

			temp = in_mat[j * n + i];
			for (k = 0; k < n; k++) {
				out_mat[j * n + k] ^= gf_mul(temp, out_mat[i * n + k]);
				in_mat[j * n + k] ^= gf_mul(temp, in_mat[i * n + k]);
			}
		}
	}
	return 0;
}

#define NO_INVERT_MATRIX -2
// Generate decode matrix from encode matrix                    从编码矩阵中生成解码矩阵
static int gf_gen_decode_matrix(unsigned char *encode_matrix,
				unsigned char *decode_matrix,
				unsigned char *invert_matrix,
				unsigned int *decode_index,
				unsigned char *src_err_list,
				unsigned char *src_in_err,
				int nerrs, int nsrcerrs, int k, int m)
{
	int i, j, p;
	int r;
	unsigned char *backup, *b, s;
	int incr = 0;

	b = malloc(MMAX * KMAX);                                     /*备份矩阵*/
	backup = malloc(MMAX * KMAX);

	if (b == NULL || backup == NULL) {                           /*惯用套路w*/
		printf("Test failure! Error with malloc\n");
		free(b);
		free(backup);
		return -1;
	}
	// Construct matrix b by removing error rows                  /*移除错误行来形成矩阵b*/
	for (i = 0, r = 0; i < k; i++, r++) {
		while (src_in_err[r])
			r++;
		for (j = 0; j < k; j++) {
			b[k * i + j] = encode_matrix[k * r + j];
			backup[k * i + j] = encode_matrix[k * r + j];
		}
		decode_index[i] = r;                                      /*记录各行原来是哪一个*/
	}
	incr = 0;
	while (gf_invert_matrix(b, invert_matrix, k) < 0) {          /*函数见ec_base122行，求逆输出在nvert_matrix中，成功返回0，失败为-1*/
		if (nerrs == (m - k)) {                                  /*所有冗余数据都有错误，无法还原*/
			free(b);
			free(backup);
			printf("BAD MATRIX\n");
			return NO_INVERT_MATRIX;
		}
		incr++;
		memcpy(b, backup, MMAX * KMAX);
		for (i = nsrcerrs; i < nerrs - nsrcerrs; i++) {                     /*？？？？？？？？？？*/
			if (src_err_list[i] == (decode_index[k - 1] + incr)) {          /*当错误行与奇偶校验行相等时，跳过*/
				// skip the erased parity line
				incr++;
				continue;
			}
		}
		if (decode_index[k - 1] + incr >= m) {                   /*尝试了所有可能的修复方法之后，仍然无法得到一个有效的解码矩阵。表明输入的编码矩阵数据本身已经损坏或无法正确解码*/
			free(b);
			free(backup);
			printf("BAD MATRIX\n");
			return NO_INVERT_MATRIX;
		}
		decode_index[k - 1] += incr;                                              /*3.31*/
		for (j = 0; j < k; j++)
			b[k * (k - 1) + j] = encode_matrix[k * decode_index[k - 1] + j];

	};

	for (i = 0; i < nsrcerrs; i++) {
		for (j = 0; j < k; j++) {
			decode_matrix[k * i + j] = invert_matrix[k * src_err_list[i] + j];
		}
	}
	/* src_err_list from encode_matrix * invert of b for parity decoding */
	for (p = nsrcerrs; p < nerrs; p++) {
		for (i = 0; i < k; i++) {
			s = 0;
			for (j = 0; j < k; j++)
				s ^= gf_mul(invert_matrix[j * k + i],
					    encode_matrix[k * src_err_list[p] + j]);

			decode_matrix[k * p + i] = s;
		}
	}
	free(b);
	free(backup);
	return 0;
}

void decode_function(int m,int k,unsigned char *buffs[TEST_SOURCES],
                     unsigned char src_in_err[TEST_SOURCES], unsigned char src_err_list[TEST_SOURCES],
                     int nerrs, int nsrcerrs, unsigned char *temp_buffs[TEST_SOURCES])
{
    int re = -1;
	int i, j, p, rtest;
	void *buf;
	unsigned int decode_index[MMAX];
	unsigned char *encode_matrix = NULL, *decode_matrix = NULL, *invert_matrix =
	    NULL, *g_tbls = NULL;
	unsigned char *recov[TEST_SOURCES];
	int rows, align, size;
	unsigned char *efence_buffs[TEST_SOURCES];
	unsigned int offset;
	u8 *ubuffs[TEST_SOURCES];
	u8 *temp_ubuffs[TEST_SOURCES];
    
    //同前
    encode_matrix = malloc(MMAX * KMAX);                   /*动态分配矩阵内存*/
	decode_matrix = malloc(MMAX * KMAX);
	invert_matrix = malloc(MMAX * KMAX);
	g_tbls = malloc(KMAX * TEST_SOURCES * 32);
	if (encode_matrix == NULL || decode_matrix == NULL
	    || invert_matrix == NULL || g_tbls == NULL) {
		printf("Test failure! Error with malloc\n");
		goto exit;
	}

    assert((m <= MMAX) && (k <= KMAX));

    //Generate encode matrix encode_matrix
	// The matrix generated by gf_gen_rs_matrix
	// is not always invertable.
	gf_gen_rs_matrix(encode_matrix, m, k);

    // Generate g_tbls from encode matrix encode_matrix
	ec_init_tables(k, m - k, &encode_matrix[k * k], g_tbls);       /*将编码矩阵中的冗余数据提取出来，并初始化到编码表 g_tbls 中，以便在编码过程中进行乘法运算。*/

    //新的
    // Generate decode matrix
	re = gf_gen_decode_matrix(encode_matrix, decode_matrix,
				  invert_matrix, decode_index, src_err_list, src_in_err,
				  nerrs, nsrcerrs, k, m);
	if (re != 0) {
		printf("Fail to gf_gen_decode_matrix\n");
		goto exit;
	}

    // Pack recovery array as list of valid sources                      将恢复数组打包为有效来源列表
	// Its order must be the same as the order                           其顺序必须与在gf_gen_decode_matrix中生成矩阵b的顺序相同
	// to generate matrix b in gf_gen_decode_matrix
	for (i = 0; i < k; i++) {
		recov[i] = buffs[decode_index[i]];                               /*完成了恢复数组的构建*/
	}

	// Recover data                                      初始化后，调用ec_encode_data函数进行数据恢复
	ec_init_tables(k, nerrs, decode_matrix, g_tbls);
	ec_encode_data(TEST_LEN, k, nerrs, g_tbls, recov, &temp_buffs[k]);

	 exit:
	free(encode_matrix);
	free(decode_matrix);
	free(invert_matrix);
	free(g_tbls);
}
