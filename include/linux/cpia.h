#ifndef cpia_h
#define cpia_h

/*
 * CPiA Parallel Port Video4Linux driver
 *
 * Supports CPiA based parallel port Video Camera's.
 *
 * (C) Copyright 1999 Bas Huisman,
 *                    Peter Pregler,
 *                    Scott J. Bertin,
 *                    VLSI Vision Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define CPIA_MAJ_VER	0
#define CPIA_MIN_VER    5
#define CPIA_PATCH_VER	0

#define CPIA_PP_MAJ_VER       0
#define CPIA_PP_MIN_VER       5
#define CPIA_PP_PATCH_VER     0

#define CPIA_MAX_FRAME_SIZE_UNALIGNED	(352 * 288 * 4)   /* CIF at RGB32 */
#define CPIA_MAX_FRAME_SIZE	((CPIA_MAX_FRAME_SIZE_UNALIGNED + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)) /* align above to PAGE_SIZE */

#ifdef __KERNEL__

#include <asm/uaccess.h>

struct cpia_camera_ops
{
	/* open sets privdata to point to structure for this camera.
         * Returns negative value on error, otherwise 0.
	 */
	int (*open)(int camnr, void **privdata);
	
	/* Registers callback function cb to be called with cbdata
	 * when an image is ready.  If cb is NULL, only single image grabs
	 * should be used.  cb should immediately call streamRead to read
	 * the data or data may be lost. Returns negative value on error,
	 * otherwise 0.
	 */
	int (*registerCallback)(void *privdata, void (*cb)(void *cbdata),
	                        void *cbdata);
	
	/* transferCmd sends commands to the camera.  command MUST point to
	 * an  8 byte buffer in kernel space. data can be NULL if no extra
	 * data is needed.  The size of the data is given by the last 2
	 * bytes of comand.  data must also point to memory in kernel space.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*transferCmd)(void *privdata, u8 *command, u8 *data);

	/* streamStart initiates stream capture mode.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*streamStart)(void *privdata);
	
	/* streamStop terminates stream capture mode.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*streamStop)(void *privdata);
        
	/* streamRead reads a frame from the camera.  buffer points to a
         * buffer large enough to hold a complete frame in kernel space.
         * noblock indicates if this should be a non blocking read.
	 * Returns the number of bytes read, or negative value on error.
         */
	int (*streamRead)(void *privdata, u8 *buffer, int noblock);
	
	/* close disables the device until open() is called again.
	 * Returns negative value on error, otherwise 0.
	 */
	int (*close)(void *privdata);
};

/* cpia_register_camera is called by low level driver for each camera.
 * A unique camera number is returned, or a negative value on error */
int cpia_register_camera(struct cpia_camera_ops *ops);

/* cpia_unregister_camera is called by low level driver when a camera
 * is removed.  This must not fail. */
void cpia_unregister_camera(int camnr);

#define CPIA_MAXCAMS		4

/* raw CIF + 64 byte header + (2 bytes line_length + EOL) per line + 4*EOI +
 * one byte 16bit DMA alignment
 */
#define CPIA_MAX_IMAGE_SIZE ((352*288*2)+64+(288*3)+5)

/* constant value's */
#define MAGIC_0		0x19
#define MAGIC_1		0x68
#define DATA_IN		0xC0
#define DATA_OUT	0x40
#define VIDEOSIZE_QCIF	0	/* 176x144 */
#define VIDEOSIZE_CIF	1	/* 352x288 */
#define VIDEOSIZE_SIF	2	/* 320x240 */
#define VIDEOSIZE_QSIF	3	/* 160x120 */
#define VIDEOSIZE_48_48		4 /* where no one has gone before, iconsize! */
#define VIDEOSIZE_64_48		5
#define VIDEOSIZE_128_96	6
#define VIDEOSIZE_160_120	VIDEOSIZE_QSIF
#define VIDEOSIZE_176_144	VIDEOSIZE_QCIF
#define VIDEOSIZE_192_144	7
#define VIDEOSIZE_224_168	8
#define VIDEOSIZE_256_192	9
#define VIDEOSIZE_288_216	10
#define VIDEOSIZE_320_240	VIDEOSIZE_SIF
#define VIDEOSIZE_352_288	VIDEOSIZE_CIF
#define SUBSAMPLE_420	0
#define SUBSAMPLE_422	1
#define YUVORDER_YUYV	0
#define YUVORDER_UYVY	1
#define NOT_COMPRESSED	0
#define COMPRESSED	1
#define NO_DECIMATION	0
#define DECIMATION_ENAB	1
#define EOI		0xff	/* End Of Image */
#define EOL		0xfd	/* End Of Line */

/* Image grab modes */
#define CPIA_GRAB_SINGLE	0
#define CPIA_GRAB_CONTINUOUS	1

/* Compression parameters */
#define CPIA_COMPRESSION_NONE	0
#define CPIA_COMPRESSION_AUTO	1
#define CPIA_COMPRESSION_MANUAL	2
#define CPIA_COMPRESSION_TARGET_QUALITY         0
#define CPIA_COMPRESSION_TARGET_FRAMERATE       1

/* Return offsets for GetCameraState */
#define SYSTEMSTATE	0
#define GRABSTATE	1
#define STREAMSTATE	2
#define FATALERROR	3
#define CMDERROR	4
#define DEBUGFLAGS	5
#define VPSTATUS	6
#define ERRORCODE	7

/* SystemState */
#define UNINITIALISED_STATE	0
#define PASS_THROUGH_STATE	1
#define LO_POWER_STATE		2
#define HI_POWER_STATE		3
#define WARM_BOOT_STATE		4

/* GrabState */
#define GRAB_IDLE		0
#define GRAB_ACTIVE		1
#define GRAB_DONE		2

/* StreamState */
#define STREAM_NOT_READY	0
#define STREAM_READY		1
#define STREAM_OPEN		2
#define STREAM_PAUSED		3
#define STREAM_FINISHED		4

/* Fatal Error, CmdError, and DebugFlags */
#define CPIA_FLAG	  1
#define SYSTEM_FLAG	  2
#define INT_CTRL_FLAG	  4
#define PROCESS_FLAG	  8
#define COM_FLAG	 16
#define VP_CTRL_FLAG	 32
#define CAPTURE_FLAG	 64
#define DEBUG_FLAG	128

/* VPStatus */
#define VP_STATE_OK			0x00

#define VP_STATE_FAILED_VIDEOINIT	0x01
#define VP_STATE_FAILED_AECACBINIT	0x02
#define VP_STATE_AEC_MAX		0x04
#define VP_STATE_ACB_BMAX		0x08

#define VP_STATE_ACB_RMIN		0x10
#define VP_STATE_ACB_GMIN		0x20
#define VP_STATE_ACB_RMAX		0x40
#define VP_STATE_ACB_GMAX		0x80

/* ErrorCode */
#define ERROR_FLICKER_BELOW_MIN_EXP     0x01 /*flicker exposure got below minimum exposure */

#define ALOG(lineno,fmt,args...) printk(fmt,lineno,##args)
#define LOG(fmt,args...) ALOG((__LINE__),KERN_INFO __FILE__":"__FUNCTION__"(%d):"fmt,##args)

#ifdef _CPIA_DEBUG_
#define ADBG(lineno,fmt,args...) printk(fmt, jiffies, lineno, ##args)
#define DBG(fmt,args...) ADBG((__LINE__),KERN_DEBUG __FILE__"(%ld):"__FUNCTION__"(%d):"fmt,##args)
#else
#define DBG(fmn,args...) {}
#endif

#define DEB_BYTE(p)\
  DBG("%1d %1d %1d %1d %1d %1d %1d %1d \n",\
      (p)&0x80?1:0, (p)&0x40?1:0, (p)&0x20?1:0, (p)&0x10?1:0,\
        (p)&0x08?1:0, (p)&0x04?1:0, (p)&0x02?1:0, (p)&0x01?1:0);

#endif /* __KERNEL__ */

#endif /* cpia_h */
