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
 * afsk2400 specific tables (tcm3105 clk 7372800.000000Hz)
 */
#define AFSK24_TX_FREQ_LO 1995
#define AFSK24_TX_FREQ_HI 3658
#define AFSK24_BITPLL_INC 9830
#define AFSK24_SAMPLERATE 16000

#if defined(CONFIG_SOUNDMODEM_FLOAT) && (defined(CONFIG_M586) || defined(CONFIG_M686))

static const float afsk24_tx_lo_i_f[] = {
	 0.080000, 0.093978, 0.000901, -0.340966, -0.703104, -0.630337, -0.009565, 0.689719, 0.884241, 0.504357, 0.007829, -0.193530, -0.132665, -0.057744 
};
#define SUM_AFSK24_TX_LO_Q 0.193114

static const float afsk24_tx_lo_q_f[] = {
	 0.000000, 0.093674, 0.278689, 0.344288, 0.004545, -0.620233, -0.986587, -0.705501, -0.011431, 0.489897, 0.484490, 0.200535, 0.002573, -0.055368 
};
#define SUM_AFSK24_TX_LO_Q -0.480430

static const float afsk24_tx_hi_i_f[] = {
	 0.080000, 0.017718, -0.268752, -0.189488, 0.604617, 0.548894, -0.684869, -0.795300, 0.423482, 0.656685, -0.111187, -0.277364, -0.004819, 0.078843 
};
#define SUM_AFSK24_TX_HI_I 0.078460

static const float afsk24_tx_hi_q_f[] = {
	 0.000000, 0.131502, 0.073759, -0.445966, -0.358907, 0.693346, 0.710211, -0.583904, -0.776322, 0.251276, 0.471624, -0.027157, -0.132603, -0.013555 
};
#define SUM_AFSK24_TX_HI_Q -0.006695

#else /* CONFIG_SOUNDMODEM_FLOAT */

static const int afsk24_tx_lo_i[] = {
	   10,   11,    0,  -43,  -89,  -80,   -1,   87,  112,   64,    0,  -24,  -16,   -7 
};
#define SUM_AFSK24_TX_LO_I 24

static const int afsk24_tx_lo_q[] = {
	    0,   11,   35,   43,    0,  -78, -125,  -89,   -1,   62,   61,   25,    0,   -7 
};
#define SUM_AFSK24_TX_LO_Q -63

static const int afsk24_tx_hi_i[] = {
	   10,    2,  -34,  -24,   76,   69,  -86, -101,   53,   83,  -14,  -35,    0,   10 
};
#define SUM_AFSK24_TX_HI_I 9

static const int afsk24_tx_hi_q[] = {
	    0,   16,    9,  -56,  -45,   88,   90,  -74,  -98,   31,   59,   -3,  -16,   -1 
};
#define SUM_AFSK24_TX_HI_Q 0

#endif /* CONFIG_SOUNDMODEM_FLOAT */

