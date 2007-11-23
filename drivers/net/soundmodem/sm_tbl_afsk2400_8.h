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
 * afsk2400 specific tables (tcm3105 clk 8000000.000000Hz)
 */
#define AFSK24_TX_FREQ_LO 2165
#define AFSK24_TX_FREQ_HI 3970
#define AFSK24_BITPLL_INC 9830
#define AFSK24_SAMPLERATE 16000

#if defined(CONFIG_SOUNDMODEM_FLOAT) && (defined(CONFIG_M586) || defined(CONFIG_M686))

static const float afsk24_tx_lo_i_f[] = {
	 0.080000, 0.087528, -0.036161, -0.402576, -0.679443, -0.392668, 0.375435, 0.933404, 0.767214, 0.139590, -0.293477, -0.278013, -0.094264, 0.004828 
};
#define SUM_AFSK24_TX_LO_Q 0.211398

static const float afsk24_tx_lo_q_f[] = {
	 0.000000, 0.099728, 0.276334, 0.269674, -0.180921, -0.792354, -0.912411, -0.319691, 0.439767, 0.689122, 0.385569, 0.019419, -0.093386, -0.079854 
};
#define SUM_AFSK24_TX_LO_Q -0.199004

static const float afsk24_tx_hi_i_f[] = {
	 0.080000, 0.001552, -0.278614, -0.016995, 0.702349, 0.051673, -0.984206, -0.080669, 0.880448, 0.073859, -0.481244, -0.035748, 0.131386, 0.012114 
};
#define SUM_AFSK24_TX_HI_I 0.055907

static const float afsk24_tx_hi_q_f[] = {
	 0.000000, 0.132681, 0.006517, -0.484255, -0.032875, 0.882804, 0.069165, -0.983330, -0.082604, 0.699228, 0.056531, -0.276388, -0.018558, 0.079077 
};
#define SUM_AFSK24_TX_HI_Q 0.047994

#else /* CONFIG_SOUNDMODEM_FLOAT */

static const int afsk24_tx_lo_i[] = {
	   10,   11,   -4,  -51,  -86,  -49,   47,  118,   97,   17,  -37,  -35,  -11,    0 
};
#define SUM_AFSK24_TX_LO_I 27

static const int afsk24_tx_lo_q[] = {
	    0,   12,   35,   34,  -22, -100, -115,  -40,   55,   87,   48,    2,  -11,  -10 
};
#define SUM_AFSK24_TX_LO_Q -25

static const int afsk24_tx_hi_i[] = {
	   10,    0,  -35,   -2,   89,    6, -124,  -10,  111,    9,  -61,   -4,   16,    1 
};
#define SUM_AFSK24_TX_HI_I 6

static const int afsk24_tx_hi_q[] = {
	    0,   16,    0,  -61,   -4,  112,    8, -124,  -10,   88,    7,  -35,   -2,   10 
};
#define SUM_AFSK24_TX_HI_Q 5

#endif /* CONFIG_SOUNDMODEM_FLOAT */

