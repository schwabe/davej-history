/*++
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *   ostypes.h
 *
 * Abstract: Holds all of the O/S specific types.
 *
 --*/
/*------------------------------------------------------------------------------
 *              D E F I N E S
 *----------------------------------------------------------------------------*/
#ifndef _OSTYPES_H_
#define _OSTYPES_H_

#define MAXIMUM_NUM_CONTAINERS	64		// 4 Luns * 16 Targets
#define MAXIMUM_NUM_ADAPTERS	8

#define OS_ALLOC_MEM_SLEEP		GFP_KERNEL

#define Os_remove_softintr OsSoftInterruptRemove
#define OsPrintf printk
#define FsaCommPrint OsPrintf

// the return values for copy_from_user & copy_to_user is the 
// number of bytes not transferred. Thus if an internal error 
// occurs, the return value is greater than zero.
#define COPYIN(SRC,DST,COUNT,FLAGS)  copy_from_user(DST,SRC,COUNT)
#define COPYOUT(SRC,DST,COUNT,FLAGS) copy_to_user(DST,SRC,COUNT)

#define copyin(SRC,DST,COUNT) copy_from_user(DST,SRC,COUNT)
#define copyout(SRC,DST,COUNT) copy_to_user(DST,SRC,COUNT)

/*------------------------------------------------------------------------------
 *              S T R U C T S / T Y P E D E F S
 *----------------------------------------------------------------------------*/
typedef struct OS_MUTEX
{
	unsigned long lock_var;
	struct wait_queue * wq_ptr;
	unsigned owner;
} OS_MUTEX;

typedef	struct OS_SPINLOCK
{
	spinlock_t	spin_lock;
	unsigned cpu_lock_count[8];
	long cpu_flag;
	long lockout_count;
} OS_SPINLOCK;

#ifdef CVLOCK_USE_SPINLOCK
	typedef OS_SPINLOCK OS_CVLOCK;
#else
	typedef OS_MUTEX OS_CVLOCK;
#endif

typedef size_t		OS_SIZE_T;

typedef	struct OS_CV_T
{
	unsigned long lock_var;
	unsigned long type;
	struct wait_queue *wq_ptr;	
} OS_CV_T;

struct fsa_scsi_hba {
	void			*CommonExtension;
	unsigned long		ContainerSize[MAXIMUM_NUM_CONTAINERS];
	unsigned long		ContainerType[MAXIMUM_NUM_CONTAINERS];
	unsigned char		ContainerValid[MAXIMUM_NUM_CONTAINERS];
	unsigned char		ContainerReadOnly[MAXIMUM_NUM_CONTAINERS];
	unsigned char		ContainerLocked[MAXIMUM_NUM_CONTAINERS];
	unsigned char		ContainerDeleted[MAXIMUM_NUM_CONTAINERS];
	long			ContainerDevNo[MAXIMUM_NUM_CONTAINERS];
};

typedef struct fsa_scsi_hba fsadev_t;

typedef struct OsKI
{
	struct Scsi_Host *scsi_host_ptr;
	void * dip;	// #REVISIT#
	fsadev_t fsa_dev;
	int thread_pid;
	int    MiniPortIndex;
} OsKI_t;

#define dev_info_t	fsadev_t

typedef int	OS_SPINLOCK_COOKIE;

typedef unsigned int	OS_STATUS;

typedef struct tq_struct OS_SOFTINTR;

typedef	OS_SOFTINTR	*ddi_softintr_t;



//-----------------------------------------------------------------------------
// Conditional variable functions

void OsCv_init ( 
	OS_CV_T *cv_ptr );


//-----------------------------------------------------------------------------
// Printing functions
void printk_err(int flag, char *fmt, ...);

#define cmn_err printk_err


//
// just ignore these solaris ddi functions in the code
//
#define DDI_SUCCESS 						0

#define ddi_add_softintr(A,B,C,D,E,F,G)		OsSoftInterruptAdd(C,F,G)

//#REVIEW#
#define ddi_remove_softintr(A)				0
#define ddi_get_soft_iblock_cookie(A, B, C)	0

#define ASSERT(expr) ((void) 0)
#define drv_usecwait udelay

#endif // _OSTYPES_H_
