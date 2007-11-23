/*
 *  drivers/s390/char/hwc_tty.h
 *    interface to the HWC-terminal driver
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 */

#ifndef __HWC_TTY_H__
#define __HWC_TTY_H__

#include <linux/ioctl.h>

#define HWC_TTY_MAX_CNTL_SIZE	20

typedef struct {
	unsigned char intr_char[HWC_TTY_MAX_CNTL_SIZE];
	unsigned char intr_char_size;
} hwc_tty_ioctl_t;

static hwc_tty_ioctl_t _ioctl;

#define HWC_TTY_IOCTL_LETTER 'B'

#define TIOCHWCTTYSINTRC _IOW(HWC_TTY_IOCTL_LETTER, 40, _ioctl.intr_char)

#define TIOCHWCTTYGINTRC _IOR(HWC_TTY_IOCTL_LETTER, 41, _ioctl.intr_char)

#endif
