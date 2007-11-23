/*
 * THIS FILE IS GENERATED AUTOMATICALLY BY ./gentbl, DO NOT EDIT!
 */


#include <linux/config.h>


/*
 * small cosine table in U8 format
 */
#define OFFSCOSTABBITS 6
#define OFFSCOSTABSIZE (1<<OFFSCOSTABBITS)

static unsigned char offscostab[OFFSCOSTABSIZE] = {
	 255, 254, 252, 249, 245, 240, 233, 226,
	 217, 208, 198, 187, 176, 164, 152, 140,
	 128, 115, 103,  91,  79,  68,  57,  47,
	  38,  29,  22,  15,  10,   6,   3,   1,
	   1,   1,   3,   6,  10,  15,  22,  29,
	  38,  47,  57,  68,  79,  91, 103, 115,
	 127, 140, 152, 164, 176, 187, 198, 208,
	 217, 226, 233, 240, 245, 249, 252, 254
};

#define OFFSCOS(x) offscostab[((x)>>10)&0x3f]


/*
 * more accurate cosine table
 */

static const short costab[64] = {
	 32767,  32609,  32137,  31356,  30272,  28897,  27244,  25329, 
	 23169,  20787,  18204,  15446,  12539,   9511,   6392,   3211, 
	     0,  -3211,  -6392,  -9511, -12539, -15446, -18204, -20787, 
	-23169, -25329, -27244, -28897, -30272, -31356, -32137, -32609, 
	-32767, -32609, -32137, -31356, -30272, -28897, -27244, -25329, 
	-23169, -20787, -18204, -15446, -12539,  -9511,  -6392,  -3211, 
	     0,   3211,   6392,   9511,  12539,  15446,  18204,  20787, 
	 23169,  25329,  27244,  28897,  30272,  31356,  32137,  32609
};

#define COS(x) costab[((x)>>10)&0x3f]
#define SIN(x) COS((x)+0xc000)


/*
 * afsk1200 specific tables
 */
#define AFSK12_SAMPLE_RATE 9600
#define AFSK12_TX_FREQ_LO 1200
#define AFSK12_TX_FREQ_HI 2200
#define AFSK12_CORRLEN 8

#if defined(CONFIG_SOUNDMODEM_FLOAT) && (defined(CONFIG_M586) || defined(CONFIG_M686))

static const float afsk12_tx_lo_i_f[] = {
	 1.000000, 0.707107, 0.000000, -0.707107, -1.000000, -0.707107, -0.000000, 0.707107 
};
#define SUM_AFSK12_TX_LO_Q 0.000000

static const float afsk12_tx_lo_q_f[] = {
	 0.000000, 0.707107, 1.000000, 0.707107, 0.000000, -0.707107, -1.000000, -0.707107 
};
#define SUM_AFSK12_TX_LO_Q 0.000000

static const float afsk12_tx_hi_i_f[] = {
	 1.000000, 0.130526, -0.965926, -0.382683, 0.866025, 0.608761, -0.707107, -0.793353 
};
#define SUM_AFSK12_TX_HI_I -0.243756

static const float afsk12_tx_hi_q_f[] = {
	 0.000000, 0.991445, 0.258819, -0.923880, -0.500000, 0.793353, 0.707107, -0.608761 
};
#define SUM_AFSK12_TX_HI_Q 0.718083

#else /* CONFIG_SOUNDMODEM_FLOAT */

static const int afsk12_tx_lo_i[] = {
	  127,   89,    0,  -89, -127,  -89,    0,   89 
};
#define SUM_AFSK12_TX_LO_I 0

static const int afsk12_tx_lo_q[] = {
	    0,   89,  127,   89,    0,  -89, -127,  -89 
};
#define SUM_AFSK12_TX_LO_Q 0

static const int afsk12_tx_hi_i[] = {
	  127,   16, -122,  -48,  109,   77,  -89, -100 
};
#define SUM_AFSK12_TX_HI_I -30

static const int afsk12_tx_hi_q[] = {
	    0,  125,   32, -117,  -63,  100,   89,  -77 
};
#define SUM_AFSK12_TX_HI_Q 89

#endif /* CONFIG_SOUNDMODEM_FLOAT */

