/*
 *    Disk Array driver for Compaq SMART2 Controllers
 *    Copyright 1998 Compaq Computer Corporation
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to arrays@compaq.com
 *
 *    If you want to make changes, improve or add functionality to this
 *    driver, you'll probably need the Compaq Array Controller Interface
 *    Specificiation (Document number ECG086/1198)
 */
#ifndef CPQARRAY_H
#define CPQARRAY_H

#ifdef __KERNEL__
#include <linux/blkdev.h>
#include <linux/locks.h>
#include <linux/malloc.h>
#include <linux/md.h>
#include <linux/timer.h>
#endif

#include "ida_cmd.h"

#define IO_OK		0
#define IO_ERROR	1
#define NWD		16
#define NWD_SHIFT	4

#define IDA_TIMER	(5*HZ)
#define IDA_TIMEOUT	(10*HZ)

#define MISC_NONFATAL_WARN	0x01

typedef struct {
	unsigned blk_size;
	unsigned nr_blks;
	unsigned cylinders;
	unsigned heads;
	unsigned sectors;
	int usage_count;
} drv_info_t;

#ifdef __KERNEL__
typedef struct {
	int	ctlr;
	char	devname[8];
	__u32	log_drv_map;
	__u32	drv_assign_map;
	__u32	drv_spare_map;
	__u32	mp_failed_drv_map;

	char	firm_rev[4];
	int	product;
	int	ctlr_sig;

	int	log_drives;
	int	phys_drives;

	__u32	board_id;
	__u32	vaddr;
	__u32	paddr;
	__u32	ioaddr;
	int	intr;
	int	usage_count;
	drv_info_t	drv[NWD];
	int	proc;

	cmdlist_t *reqQ;
	cmdlist_t *cmpQ;
	cmdlist_t *cmd_pool;
	__u32	*cmd_pool_bits;
	unsigned int Qdepth;
	unsigned int maxQsinceinit;

	unsigned int nr_requests;
	unsigned int nr_allocs;
	unsigned int nr_frees;
	struct timer_list timer;
	unsigned int misc_tflags;
} ctlr_info_t;
#endif

#endif /* CPQARRAY_H */
