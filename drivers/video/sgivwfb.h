/*
 *  linux/drivers/video/sgivwfb.h -- SGI DBE frame buffer device header
 *
 *      Copyright (C) 1999 Silicon Graphics, Inc.
 *      Jeffrey Newquist, newquist@engr.sgi.com
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef __SGIVWFB_H__
#define __SGIVWFB_H__

#define DBE_GETREG(reg, dest)       ((dest) = DBE_REG_BASE->##reg)
#define DBE_SETREG(reg, src)        DBE_REG_BASE->##reg = (src)
#define DBE_IGETREG(reg, idx, dest) ((dest) = DBE_REG_BASE->##reg##[idx])
#define DBE_ISETREG(reg, idx, src)  (DBE_REG_BASE->##reg##[idx] = (src))

#define MASK(msb, lsb)          ( (((u32)1<<((msb)-(lsb)+1))-1) << (lsb) )
#define GET(v, msb, lsb)        ( ((u32)(v) & MASK(msb,lsb)) >> (lsb) )
#define SET(v, f, msb, lsb)     ( (v) = ((v)&~MASK(msb,lsb)) | (( (u32)(f)<<(lsb) ) & MASK(msb,lsb)) )

#define GET_DBE_FIELD(reg, field, v)        GET((v), DBE_##reg##_##field##_MSB, DBE_##reg##_##field##_LSB)
#define SET_DBE_FIELD(reg, field, v, f)     SET((v), (f), DBE_##reg##_##field##_MSB, DBE_##reg##_##field##_LSB)

/* NOTE: All loads/stores must be 32 bits and uncached */

#define DBE_REG_PHYS    0xd0000000
#define DBE_REG_SIZE        0x01000000

typedef struct {
  volatile u32 ctrlstat;     /* 0x000000 general control */
  volatile u32 dotclock;     /* 0x000004 dot clock PLL control */
  volatile u32 i2c;          /* 0x000008 crt I2C control */
  volatile u32 sysclk;       /* 0x00000c system clock PLL control */
  volatile u32 i2cfp;        /* 0x000010 flat panel I2C control */
  volatile u32 id;           /* 0x000014 device id/chip revision */
  volatile u32 config;       /* 0x000018 power on configuration */
  volatile u32 bist;         /* 0x00001c internal bist status */

  char _pad0[ 0x010000 - 0x000020 ];

  volatile u32 vt_xy;        /* 0x010000 current dot coords */
  volatile u32 vt_xymax;     /* 0x010004 maximum dot coords */
  volatile u32 vt_vsync;     /* 0x010008 vsync on/off */
  volatile u32 vt_hsync;     /* 0x01000c hsync on/off */
  volatile u32 vt_vblank;    /* 0x010010 vblank on/off */
  volatile u32 vt_hblank;    /* 0x010014 hblank on/off */
  volatile u32 vt_flags;     /* 0x010018 polarity of vt signals */
  volatile u32 vt_f2rf_lock; /* 0x01001c f2rf & framelck y coord */
  volatile u32 vt_intr01;    /* 0x010020 intr 0,1 y coords */
  volatile u32 vt_intr23;    /* 0x010024 intr 2,3 y coords */
  volatile u32 fp_hdrv;      /* 0x010028 flat panel hdrv on/off */
  volatile u32 fp_vdrv;      /* 0x01002c flat panel vdrv on/off */
  volatile u32 fp_de;        /* 0x010030 flat panel de on/off */
  volatile u32 vt_hpixen;    /* 0x010034 intrnl horiz pixel on/off*/
  volatile u32 vt_vpixen;    /* 0x010038 intrnl vert pixel on/off */
  volatile u32 vt_hcmap;     /* 0x01003c cmap write (horiz) */
  volatile u32 vt_vcmap;     /* 0x010040 cmap write (vert) */
  volatile u32 did_start_xy; /* 0x010044 eol/f did/xy reset val */
  volatile u32 crs_start_xy; /* 0x010048 eol/f crs/xy reset val */
  volatile u32 vc_start_xy;  /* 0x01004c eol/f vc/xy reset val */

  char _pad1[ 0x020000 - 0x010050 ];

  volatile u32 ovr_width_tile; /* 0x020000 overlay plane ctrl 0 */
  volatile u32 ovr_inhwctrl;   /* 0x020004 overlay plane ctrl 1 */
  volatile u32 ovr_control;    /* 0x020008 overlay plane ctrl 1 */

  char _pad2[ 0x030000 - 0x02000C ];

  volatile u32 frm_size_tile;  /* 0x030000 normal plane ctrl 0 */
  volatile u32 frm_size_pixel; /* 0x030004 normal plane ctrl 1 */
  volatile u32 frm_inhwctrl;   /* 0x030008 normal plane ctrl 2 */
  volatile u32 frm_control;        /* 0x03000C normal plane ctrl 3 */

  char _pad3[ 0x040000 - 0x030010 ];

  volatile u32 did_inhwctrl;   /* 0x040000 DID control */
  volatile u32 did_control;    /* 0x040004 DID shadow */

  char _pad4[ 0x048000 - 0x040008 ];

  volatile u32 mode_regs[32];  /* 0x048000 - 0x04807c WID table */

  char _pad5[ 0x050000 - 0x048080 ];

  volatile u32 cmap[6144];     /* 0x050000 - 0x055ffc color map */

  char _pad6[ 0x058000 - 0x056000 ];

  volatile u32 cm_fifo;        /* 0x058000 color map fifo status */

  char _pad7[ 0x060000 - 0x058004 ];

  volatile u32 gmap[256];      /* 0x060000 - 0x0603fc gamma map */

  char _pad8[ 0x068000 - 0x060400 ];

  volatile u32 gmap10[1024];   /* 0x068000 - 0x068ffc gamma map */

  char _pad9[ 0x070000 - 0x069000 ];

  volatile u32 crs_pos;        /* 0x070000 cusror control 0 */
  volatile u32 crs_ctl;        /* 0x070004 cusror control 1 */
  volatile u32 crs_cmap[3];    /* 0x070008 - 0x070010 crs cmap */

  char _pad10[ 0x078000 - 0x070014 ];

  volatile u32 crs_glyph[64];  /* 0x078000 - 0x0780fc crs glyph */

  char _pad11[ 0x080000 - 0x078100 ];

  volatile u32 vc_0;           /* 0x080000 video capture crtl 0 */
  volatile u32 vc_1;           /* 0x080004 video capture crtl 1 */
  volatile u32 vc_2;           /* 0x080008 video capture crtl 2 */
  volatile u32 vc_3;           /* 0x08000c video capture crtl 3 */
  volatile u32 vc_4;           /* 0x080010 video capture crtl 3 */
  volatile u32 vc_5;           /* 0x080014 video capture crtl 3 */
  volatile u32 vc_6;           /* 0x080018 video capture crtl 3 */
  volatile u32 vc_7;           /* 0x08001c video capture crtl 3 */
  volatile u32 vc_8;           /* 0x08000c video capture crtl 3 */
} asregs;

/* Bit mask information */

#define DBE_CTRLSTAT_CHIPID_MSB     3
#define DBE_CTRLSTAT_CHIPID_LSB     0
#define DBE_CTRLSTAT_SENSE_N_MSB    4
#define DBE_CTRLSTAT_SENSE_N_LSB    4
#define DBE_CTRLSTAT_PCLKSEL_MSB    29
#define DBE_CTRLSTAT_PCLKSEL_LSB    28

#define DBE_DOTCLK_M_MSB            7
#define DBE_DOTCLK_M_LSB            0
#define DBE_DOTCLK_N_MSB            13
#define DBE_DOTCLK_N_LSB            8
#define DBE_DOTCLK_P_MSB            15
#define DBE_DOTCLK_P_LSB            14
#define DBE_DOTCLK_RUN_MSB          20
#define DBE_DOTCLK_RUN_LSB          20

#define DBE_VT_XY_VT_FREEZE_MSB     31
#define DBE_VT_XY_VT_FREEZE_LSB     31

#define DBE_FP_VDRV_FP_VDRV_ON_MSB        23
#define DBE_FP_VDRV_FP_VDRV_ON_LSB        12
#define DBE_FP_VDRV_FP_VDRV_OFF_MSB       11
#define DBE_FP_VDRV_FP_VDRV_OFF_LSB       0

#define DBE_FP_HDRV_FP_HDRV_ON_MSB        23
#define DBE_FP_HDRV_FP_HDRV_ON_LSB        12
#define DBE_FP_HDRV_FP_HDRV_OFF_MSB       11
#define DBE_FP_HDRV_FP_HDRV_OFF_LSB       0

#define DBE_FP_DE_FP_DE_ON_MSB        23
#define DBE_FP_DE_FP_DE_ON_LSB        12
#define DBE_FP_DE_FP_DE_OFF_MSB       11
#define DBE_FP_DE_FP_DE_OFF_LSB       0

#define DBE_VT_VSYNC_VT_VSYNC_ON_MSB        23
#define DBE_VT_VSYNC_VT_VSYNC_ON_LSB        12
#define DBE_VT_VSYNC_VT_VSYNC_OFF_MSB       11
#define DBE_VT_VSYNC_VT_VSYNC_OFF_LSB       0

#define DBE_VT_HSYNC_VT_HSYNC_ON_MSB        23
#define DBE_VT_HSYNC_VT_HSYNC_ON_LSB        12
#define DBE_VT_HSYNC_VT_HSYNC_OFF_MSB       11
#define DBE_VT_HSYNC_VT_HSYNC_OFF_LSB       0

#define DBE_VT_VBLANK_VT_VBLANK_ON_MSB        23
#define DBE_VT_VBLANK_VT_VBLANK_ON_LSB        12
#define DBE_VT_VBLANK_VT_VBLANK_OFF_MSB       11
#define DBE_VT_VBLANK_VT_VBLANK_OFF_LSB       0

#define DBE_VT_HBLANK_VT_HBLANK_ON_MSB        23
#define DBE_VT_HBLANK_VT_HBLANK_ON_LSB        12
#define DBE_VT_HBLANK_VT_HBLANK_OFF_MSB       11
#define DBE_VT_HBLANK_VT_HBLANK_OFF_LSB       0

#define DBE_VT_FLAGS_VDRV_INVERT_MSB  0
#define DBE_VT_FLAGS_VDRV_INVERT_LSB  0
#define DBE_VT_FLAGS_HDRV_INVERT_MSB  2
#define DBE_VT_FLAGS_HDRV_INVERT_LSB  2

#define DBE_VT_VCMAP_VT_VCMAP_ON_MSB        23
#define DBE_VT_VCMAP_VT_VCMAP_ON_LSB        12
#define DBE_VT_VCMAP_VT_VCMAP_OFF_MSB       11
#define DBE_VT_VCMAP_VT_VCMAP_OFF_LSB       0

#define DBE_VT_HCMAP_VT_HCMAP_ON_MSB        23
#define DBE_VT_HCMAP_VT_HCMAP_ON_LSB        12
#define DBE_VT_HCMAP_VT_HCMAP_OFF_MSB       11
#define DBE_VT_HCMAP_VT_HCMAP_OFF_LSB       0

#define DBE_VT_XYMAX_VT_MAXX_MSB    11
#define DBE_VT_XYMAX_VT_MAXX_LSB    0
#define DBE_VT_XYMAX_VT_MAXY_MSB    23
#define DBE_VT_XYMAX_VT_MAXY_LSB    12

#define DBE_VT_HPIXEN_VT_HPIXEN_ON_MSB      23
#define DBE_VT_HPIXEN_VT_HPIXEN_ON_LSB      12
#define DBE_VT_HPIXEN_VT_HPIXEN_OFF_MSB     11
#define DBE_VT_HPIXEN_VT_HPIXEN_OFF_LSB     0

#define DBE_VT_VPIXEN_VT_VPIXEN_ON_MSB      23
#define DBE_VT_VPIXEN_VT_VPIXEN_ON_LSB      12
#define DBE_VT_VPIXEN_VT_VPIXEN_OFF_MSB     11
#define DBE_VT_VPIXEN_VT_VPIXEN_OFF_LSB     0

#define DBE_OVR_CONTROL_OVR_DMA_ENABLE_MSB  0
#define DBE_OVR_CONTROL_OVR_DMA_ENABLE_LSB  0

#define DBE_OVR_INHWCTRL_OVR_DMA_ENABLE_MSB 0
#define DBE_OVR_INHWCTRL_OVR_DMA_ENABLE_LSB 0

#define DBE_OVR_WIDTH_TILE_OVR_FIFO_RESET_MSB       13
#define DBE_OVR_WIDTH_TILE_OVR_FIFO_RESET_LSB       13

#define DBE_FRM_CONTROL_FRM_DMA_ENABLE_MSB  0
#define DBE_FRM_CONTROL_FRM_DMA_ENABLE_LSB  0
#define DBE_FRM_CONTROL_FRM_TILE_PTR_MSB    31
#define DBE_FRM_CONTROL_FRM_TILE_PTR_LSB    9
#define DBE_FRM_CONTROL_FRM_LINEAR_MSB      1
#define DBE_FRM_CONTROL_FRM_LINEAR_LSB      1

#define DBE_FRM_INHWCTRL_FRM_DMA_ENABLE_MSB 0
#define DBE_FRM_INHWCTRL_FRM_DMA_ENABLE_LSB 0

#define DBE_FRM_SIZE_TILE_FRM_WIDTH_TILE_MSB        12
#define DBE_FRM_SIZE_TILE_FRM_WIDTH_TILE_LSB        5
#define DBE_FRM_SIZE_TILE_FRM_RHS_MSB       4
#define DBE_FRM_SIZE_TILE_FRM_RHS_LSB       0
#define DBE_FRM_SIZE_TILE_FRM_DEPTH_MSB     14
#define DBE_FRM_SIZE_TILE_FRM_DEPTH_LSB     13
#define DBE_FRM_SIZE_TILE_FRM_FIFO_RESET_MSB        15
#define DBE_FRM_SIZE_TILE_FRM_FIFO_RESET_LSB        15

#define DBE_FRM_SIZE_PIXEL_FB_HEIGHT_PIX_MSB        31
#define DBE_FRM_SIZE_PIXEL_FB_HEIGHT_PIX_LSB        16

#define DBE_DID_CONTROL_DID_DMA_ENABLE_MSB  0
#define DBE_DID_CONTROL_DID_DMA_ENABLE_LSB  0
#define DBE_DID_INHWCTRL_DID_DMA_ENABLE_MSB 0
#define DBE_DID_INHWCTRL_DID_DMA_ENABLE_LSB 0

#define DBE_DID_START_XY_DID_STARTY_MSB     23
#define DBE_DID_START_XY_DID_STARTY_LSB     12
#define DBE_DID_START_XY_DID_STARTX_MSB     11
#define DBE_DID_START_XY_DID_STARTX_LSB     0

#define DBE_CRS_START_XY_CRS_STARTY_MSB     23
#define DBE_CRS_START_XY_CRS_STARTY_LSB     12
#define DBE_CRS_START_XY_CRS_STARTX_MSB     11
#define DBE_CRS_START_XY_CRS_STARTX_LSB     0

#define DBE_WID_TYP_MSB     4
#define DBE_WID_TYP_LSB     2
#define DBE_WID_BUF_MSB     1
#define DBE_WID_BUF_LSB     0

#define DBE_VC_START_XY_VC_STARTY_MSB       23
#define DBE_VC_START_XY_VC_STARTY_LSB       12
#define DBE_VC_START_XY_VC_STARTX_MSB       11
#define DBE_VC_START_XY_VC_STARTX_LSB       0

/* Constants */

#define DBE_FRM_DEPTH_8     0
#define DBE_FRM_DEPTH_16    1
#define DBE_FRM_DEPTH_32    2

#define DBE_CMODE_I8        0
#define DBE_CMODE_I12       1
#define DBE_CMODE_RG3B2     2
#define DBE_CMODE_RGB4      3
#define DBE_CMODE_ARGB5     4
#define DBE_CMODE_RGB8      5
#define DBE_CMODE_RGBA5     6
#define DBE_CMODE_RGB10     7

#define DBE_BMODE_BOTH      3

#define DBE_CRS_MAGIC       54

#define DBE_CLOCK_REF_KHZ 27000

/* Config Register (DBE Only) Definitions */

#define DBE_CONFIG_VDAC_ENABLE       0x00000001
#define DBE_CONFIG_VDAC_GSYNC        0x00000002
#define DBE_CONFIG_VDAC_PBLANK       0x00000004
#define DBE_CONFIG_FPENABLE          0x00000008
#define DBE_CONFIG_LENDIAN           0x00000020
#define DBE_CONFIG_TILEHIST          0x00000040
#define DBE_CONFIG_EXT_ADDR          0x00000080

#define DBE_CONFIG_FBDEV        ( DBE_CONFIG_VDAC_ENABLE | \
                                      DBE_CONFIG_VDAC_GSYNC  | \
                                      DBE_CONFIG_VDAC_PBLANK | \
                                      DBE_CONFIG_LENDIAN     | \
                                      DBE_CONFIG_EXT_ADDR )

#endif
