/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/page.h>
#include "ppc_asm.tmpl"
#include "ppc_defs.h"

#ifndef CONFIG_8xx
/* This instruction is not implemented on the PPC 601 or 603 */
#define tlbia \
	li	r4,128; \
	mtspr	CTR,r4; \
	li	r4,0; \
0:	tlbie	r4; \
	addi	r4,r4,0x1000; \
	bdnz	0b
#endif	
	.text

/*
 * Returns (address we're running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.
 */

_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	lis	r4,1b@ha
	addi	r4,r4,1b@l
	subf	r3,r4,r3
	mtlr	r0
	blr

/*
 * Disable interrupts
 *	rc = _disable_interrupts()
 */
_GLOBAL(_disable_interrupts)
_GLOBAL(__cli)
_GLOBAL(_hard_cli)
	mfmsr	r0		/* Get current interrupt state */
	rlwinm	r3,r0,16+1,32-1,31	/* Extract old value of 'EE' */
	li	r4,0		/* Need [unsigned] value of MSR_EE */
	ori	r4,r4,MSR_EE	/* Set to turn off bit */
	andc	r0,r0,r4	/* Clears bit in (r4) */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0		/* Update machine state */
	blr			/* Done */

/*
 * Enable interrupts
 *	_enable_interrupts(int state)
 * turns on interrupts if state = 1.
 */
_GLOBAL(_enable_interrupts)
	cmpi	0,r3,0		/* turning them on? */
	beqlr			/* nothing to do if state == 0 */
_GLOBAL(__sti)
_GLOBAL(_hard_sti)
	lis	r4,ppc_n_lost_interrupts@ha
	lwz	r4,ppc_n_lost_interrupts@l(r4)
	mfmsr	r3		/* Get current state */
	ori	r3,r3,MSR_EE	/* Turn on 'EE' bit */
	cmpi	0,r4,0		/* lost interrupts to process first? */
	bne-	do_lost_interrupts
	sync			/* Some chip revs have problems here... */
	mtmsr	r3		/* Update machine state */
	blr

/*
 * We were about to enable interrupts but we have to simulate
 * some interrupts that were lost by enable_irq first.
 */
	.globl do_lost_interrupts
do_lost_interrupts:
	stwu	r1,-16(r1)
	mflr	r0
	stw	r0,20(r1)
	stw	r3,8(r1)
1:	bl	fake_interrupt
	lis	r4,ppc_n_lost_interrupts@ha
	lwz	r4,ppc_n_lost_interrupts@l(r4)
	cmpi	0,r4,0
	bne-	1b
	lwz	r3,8(r1)
	sync
	mtmsr	r3
	lwz	r0,20(r1)
	mtlr	r0
	addi	r1,r1,16
	blr


/*
 * complement mask on the msr then "or" some values on.
 *     _nmask_and_or_msr(nmask, value_to_or)
 */
	_GLOBAL(_nmask_and_or_msr)
	mfmsr	r0		/* Get current msr */
	andc	r0,r0,r3	/* And off the bits set in r3 (first parm) */
	or	r0,r0,r4		/* Or on the bits in r4 (second parm) */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0		/* Update machine state */
	blr			/* Done */


/*
 * Flush MMU TLB
 */
_GLOBAL(_tlbia)
#if defined(CONFIG_SMP)
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,10
10:	lwarx	r7,0,r9
	cmpi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
#endif /* CONFIG_SMP */
	sync
	tlbia
	sync
#ifdef __SMP__
	tlbsync
	sync
	li	r0,0
	stw	r0,0(r9)		/* clear hash_table_lock */
	mtmsr	r10
	SYNC
#endif
	blr	

/*
 * Flush MMU TLB for a particular address
 */
_GLOBAL(_tlbie)
#if defined(CONFIG_SMP)
	mfmsr	r10
	sync
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	mtmsr	r0
	SYNC
	lis	r9,hash_table_lock@h
	ori	r9,r9,hash_table_lock@l
	lwz	r8,PROCESSOR(r2)
	oris	r8,r8,11
10:	lwarx	r7,0,r9
	cmpi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
#endif /* CONFIG_SMP */
	tlbie	r3
	sync
#ifdef CONFIG_SMP
	tlbsync
	sync
	li	r0,0
	stw	r0,0(r9)		/* clear hash_table_lock */
	mtmsr	r10
	SYNC
#endif
	blr

/*
 * The atomic operations here generally act as memory barriers too on SMP.
 * The general usage of them assumes this since x86 processors won't
 * move a load past a lock instruction and don't reorder stores.
 */
#ifdef __SMP__
#define SMP_WMB	eieio
#define SMP_MB	sync
#else
#define SMP_WMB
#define SMP_MB
#endif /* CONFIG_SMP */

/*
 * Atomic [test&set] exchange
 *
 *	unsigned long xchg_u32(void *ptr, unsigned long val)
 * Changes the memory location '*ptr' to be val and returns
 * the previous value stored there.
 */
_GLOBAL(xchg_u32)
	mr	r5,r3		/* Save pointer */
	SMP_WMB
10:	lwarx	r3,0,r5		/* Fetch old value & reserve */
	stwcx.	r4,0,r5		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	SMP_MB
	blr

/*
 * Try to acquire a spinlock.
 * Only does the stwcx. if the load returned 0 - the Programming
 * Environments Manual suggests not doing unnecessary stcwx.'s
 * since they may inhibit forward progress by other CPUs in getting
 * a lock.
 */
_GLOBAL(__spin_trylock)
	mr	r4,r3
	SMP_WMB			/* prevent reordering of stores */
	li	r5,-1
	lwarx	r3,0,r4		/* fetch old value, establish reservation */
	cmpwi	0,r3,0		/* is it 0? */
	bnelr-			/* return failure if not */
	stwcx.	r5,0,r4		/* try to update with new value */
	bne-	1f		/* if we failed */
	SMP_WMB			/* prevent reordering of stores */
	blr
1:	li	r3,1		/* return non-zero for failure */
	blr

/*
 * Atomic add/sub/inc/dec operations
 *
 * void atomic_add(int c, int *v)
 * void atomic_sub(int c, int *v)
 * void atomic_inc(int *v)
 * void atomic_dec(int *v)
 * int atomic_dec_and_test(int *v)
 * int atomic_inc_return(int *v)
 * int atomic_dec_return(int *v)
 * void atomic_clear_mask(atomic_t mask, atomic_t *addr)
 * void atomic_set_mask(atomic_t mask, atomic_t *addr);
 */
_GLOBAL(atomic_add)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	add	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	SMP_MB
	blr
_GLOBAL(atomic_add_return)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	add	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5
	SMP_MB
	blr
_GLOBAL(atomic_sub)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	sub	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	SMP_MB
	blr
_GLOBAL(atomic_inc)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	addi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	SMP_MB
	blr
_GLOBAL(atomic_inc_return)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	addi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5		/* Return new value */
	SMP_MB
	blr
_GLOBAL(atomic_dec)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	SMP_MB
	blr
_GLOBAL(atomic_dec_return)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5		/* Return new value */
	SMP_MB
	blr
_GLOBAL(atomic_dec_and_test)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	SMP_MB
	cntlzw	r3,r5
	srwi	r3,r3,5
	blr
_GLOBAL(atomic_clear_mask)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r4
	andc	r5,r5,r3
	stwcx.	r5,0,r4
	bne-	10b
	SMP_MB
	blr
_GLOBAL(atomic_set_mask)
	SMP_WMB			/* wmb() */
10:	lwarx	r5,0,r4
	or	r5,r5,r3
	stwcx.	r5,0,r4
	bne-	10b
	SMP_MB
	blr

/*
 * I/O string operations
 *
 * insb(port, buf, len)
 * outsb(port, buf, len)
 * insw(port, buf, len)
 * outsw(port, buf, len)
 * insl(port, buf, len)
 * outsl(port, buf, len)
 * insw_ns(port, buf, len)
 * outsw_ns(port, buf, len)
 * insl_ns(port, buf, len)
 * outsl_ns(port, buf, len)
 *
 * The *_ns versions don't do byte-swapping.
 */
_GLOBAL(_insb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbz	r5,0(r3)
	eieio
	stbu	r5,1(r4)
	bdnz	00b
	blr

_GLOBAL(_outsb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbzu	r5,1(r4)
	stb	r5,0(r3)
	eieio
	bdnz	00b
	blr	

_GLOBAL(_insw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhbrx	r5,0,r3
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(_outsw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	eieio
	sthbrx	r5,0,r3	
	bdnz	00b
	blr	

_GLOBAL(_insl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwbrx	r5,0,r3
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	blr

_GLOBAL(_outsl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stwbrx	r5,0,r3
	eieio
	bdnz	00b
	blr	

_GLOBAL(ide_insw)
_GLOBAL(_insw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhz	r5,0(r3)
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(ide_outsw)
_GLOBAL(_outsw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	sth	r5,0(r3)
	eieio
	bdnz	00b
	blr	

_GLOBAL(_insl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwz	r5,0(r3)
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	blr

_GLOBAL(_outsl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stw	r5,0(r3)
	eieio
	bdnz	00b
	blr	

/*
 * Extended precision shifts.
 * 
 * Updated to be valid for shift counts from 0 to 63 inclusive.
 * -- Gabriel
 *
 * R3/R4 has 64 bit value
 * R5    has shift count
 * result in R3/R4
 *
 *  ashrdi3: arithmetic right shift (sign propagation)	    
 *  lshrdi3: logical right shift	
 *  ashldi3: left shift
 */
_GLOBAL(__ashrdi3)
	subfic	r6,r5,32	
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	rlwinm	r8,r7,0,32	# t3 = (count < 32) ? 32 : 0
	sraw	r7,r3,r7	# t2 = MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	slw	r7,r7,r8	# t2 = (count < 32) ? 0 : t2
	sraw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2
	blr

_GLOBAL(__ashldi3)
	subfic	r6,r5,32	
	slw	r3,r3,r5	# MSW = count > 31 ? 0 : MSW << count
	addi	r7,r5,32	# could be xori, or addi with -32
	srw	r6,r4,r6	# t1 = count > 31 ? 0 : LSW >> (32-count)
	slw	r7,r4,r7	# t2 = count < 32 ? 0 : LSW << (count-32)
	or	r3,r3,r6	# MSW |= t1
	slw	r4,r4,r5	# LSW = LSW << count
	or	r3,r3,r7	# MSW |= t2
	blr

_GLOBAL(__lshrdi3)
	subfic	r6,r5,32	
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	srw	r7,r3,r7	# t2 = count < 32 ? 0 : MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	srw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2 
	blr

_GLOBAL(abs)
	srawi	r4,r3,31
	xor	r3,r3,r4
	sub	r3,r3,r4
	blr

_GLOBAL(_get_SP)
	mr	r3,r1		/* Close enough */
	blr

_GLOBAL(_get_THRM1)
	mfspr	r3,THRM1
	blr

_GLOBAL(_get_THRM2)
	mfspr	r3,THRM2
	blr

_GLOBAL(_get_THRM3)
	mfspr	r3,THRM3
	blr
		
_GLOBAL(_set_THRM1)
	mtspr	THRM1,r3
	blr

_GLOBAL(_set_THRM2)
	mtspr	THRM2,r3
	blr

_GLOBAL(_set_THRM3)
	mtspr	THRM3,r3
	blr
	
_GLOBAL(_get_PVR)
	mfspr	r3,PVR
	blr
	
_GLOBAL(_get_HID0)
	mfspr	r3,HID0
	blr

_GLOBAL(_set_HID0)
	sync
	mtspr	HID0, r3
	sync
	isync /* Handle erratas in some cases */
	blr

_GLOBAL(_get_ICTC)
       mfspr   r3,ICTC
       blr

_GLOBAL(_set_ICTC)
       mtspr   ICTC,r3
       blr

/*
	L2CR functions
	Copyright © 1997-1998 by PowerLogix R & D, Inc.
	
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
	Thur, Dec. 12, 1998.
	- First public release, contributed by PowerLogix.
	***********
	Sat, Aug. 7, 1999.
	- Terry: Made sure code disabled interrupts before running. (Previously
			it was assumed interrupts were already disabled).
	- Terry: Updated for tentative G4 support.  4MB of memory is now flushed
			instead of 2MB.  (Prob. only 3 is necessary).
	- Terry: Updated for workaround to HID0[DPM] processor bug
			during global invalidates.
	***********
	Thu, July 13, 2000.
	- Terry: Added isync to correct for an errata.
	
	Author:	Terry Greeniaus (tgree@phys.ualberta.ca)
	Please e-mail updates to this file to me, thanks!
*/
/* Usage:
	
	When setting the L2CR register, you must do a few special
	things.  If you are enabling the cache, you must perform a
	global invalidate.  If you are disabling the cache, you must
	flush the cache contents first.  This routine takes care of
	doing these things.  When first enabling the cache, make sure
	you pass in the L2CR you want, as well as passing in the
	global invalidate bit set.  A global invalidate will only be
	performed if the L2I bit is set in applyThis.  When enabling
	the cache, you should also set the L2E bit in applyThis.  If
	you want to modify the L2CR contents after the cache has been
	enabled, the recommended procedure is to first call
	__setL2CR(0) to disable the cache and then call it again with
	the new values for L2CR.  Examples:

	_setL2CR(0)		- disables the cache
	_setL2CR(0xB3A04000)	- enables my G3 upgrade card:
				- L2E set to turn on the cache
				- L2SIZ set to 1MB
				- L2CLK set to 1:1
				- L2RAM set to pipelined synchronous late-write
				- L2I set to perform a global invalidation
				- L2OH set to 0.5 nS
				- L2DF set because this upgrade card
				  requires it

	A similar call should work for your card.  You need to know
	the correct setting for your card and then place them in the
	fields I have outlined above.  Other fields support optional
	features, such as L2DO which caches only data, or L2TS which
	causes cache pushes from the L1 cache to go to the L2 cache
	instead of to main memory.
*/
/*
 * Summary: this procedure ignores the L2I bit in the value passed in,
 * flushes the cache if it was already enabled, always invalidates the
 * cache, then enables the cache if the L2E bit is set in the value
 * passed in.
 *   -- paulus.
 */
_GLOBAL(_set_L2CR)
	/* Make sure this is a 750 or 7400 chip */
	mfspr	r4,PVR
	rlwinm	r4,r4,16,16,31
	cmpwi	r4,0x0008
	cmpwi	cr1,r4,0x000c
	cror	2,2,4*cr1+2
	bne	99f

	/* Turn off interrupts and data relocation. */
	mfmsr	r7		/* Save MSR in r7 */
	rlwinm	r4,r7,0,17,15
	rlwinm	r4,r4,0,28,26	/* Turn off DR bit */
	sync
	mtmsr	r4
	sync

	/* Get the current enable bit of the L2CR into r4 */
	mfspr	r4,L2CR
	
	/* Tweak some bits */
	rlwinm	r5,r3,0,0,0		/* r5 contains the new enable bit */
	rlwinm	r3,r3,0,11,9		/* Turn off the invalidate bit */
	rlwinm	r3,r3,0,1,31		/* Turn off the enable bit */

	/* Check to see if we need to flush */
	rlwinm.	r4,r4,0,0,0
	beq	2f

	/* Flush the cache. First, read the first 4MB of memory (physical) to
	 * put new data in the cache.  (Actually we only need
	 * the size of the L2 cache plus the size of the L1 cache, but 4MB will
	 * cover everything just to be safe).
	 */
		
	 /**** Might be a good idea to set L2DO here - to prevent instructions
	       from getting into the cache.  But since we invalidate
	       the next time we enable the cache it doesn't really matter.
	  ****/

	lis	r4,0x0002
	mtctr	r4
	li	r4,0
1:
	lwzx	r0,r0,r4
	addi	r4,r4,32		/* Go to start of next cache line */
	bdnz	1b
	
	/* Now, flush the first 4MB of memory */
	lis	r4,0x0002
	mtctr	r4
	li	r4,0
	sync
1:
	dcbf	r0,r4
	addi	r4,r4,32		/* Go to start of next cache line */
	bdnz	1b

2:
	/* Set up the L2CR configuration bits (and switch L2 off) */
	sync
	mtspr	L2CR,r3
	sync

	/* Before we perform the global invalidation, we must disable dynamic
	 * power management via HID0[DPM] to work around a processor bug where
	 * DPM can possibly interfere with the state machine in the processor
	 * that invalidates the L2 cache tags.
	 */
	mfspr	r8,HID0			/* Save HID0 in r8 */
	rlwinm	r4,r8,0,12,10		/* Turn off HID0[DPM] */
	sync
	mtspr	HID0,r4			/* Disable DPM */
	sync

	/* Perform a global invalidation */
	oris	r3,r3,0x0020
	sync
	mtspr	L2CR,r3
	sync
	isync				/* For errata */

	/* Wait for the invalidation to complete */
3:	mfspr	r3,L2CR
	rlwinm.	r4,r3,0,31,31
	bne	3b
	
	rlwinm	r3,r3,0,11,9		/* Turn off the L2I bit */
	sync
	mtspr	L2CR,r3
	sync
	
	/* Restore HID0[DPM] to whatever it was before */
	sync
	mtspr	1008,r8
	sync

	/* See if we need to enable the cache */
	cmplwi	r5,0
	beq	4f

	/* Enable the cache */
	oris	r3,r3,0x8000
	mtspr	L2CR,r3
	sync

	/* Restore MSR (restores EE and DR bits to original state) */
4:	sync
	mtmsr	r7
	sync
	blr

99:	li	r3,-1
	blr

_GLOBAL(_get_L2CR)
	/* Make sure this is a 750 chip */
	mfspr	r3,PVR
	srwi	r3,r3,16
	cmpwi	r3,0x0008
	cmpwi	cr1,r3,0x000c
	li	r3,0
	cror	2,2,4*cr1+2
	bnelr
	/* Return the L2CR contents */
	mfspr	r3,L2CR
	blr

/* --- End of PowerLogix code ---
 */


/*
 * These are used in the alignment trap handler when emulating
 * single-precision loads and stores.
 * We restore and save the fpscr so the task gets the same result
 * and exceptions as if the cpu had performed the load or store.
 */
_GLOBAL(cvt_fd)
cvt_fd:
	lfd	0,-4(r5)	/* load up fpscr value */
	mtfsf	0xff,0
	lfs	0,0(r3)
	stfd	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,-4(r5)
	blr

_GLOBAL(cvt_df)
cvt_df:
	lfd	0,-4(r5)	/* load up fpscr value */
	mtfsf	0xff,0
	lfd	0,0(r3)
	stfs	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,-4(r5)
	blr

        .globl  __clear_msr_me
__clear_msr_me:
        mfmsr   r0                      /* Get current interrupt state */
        lis     r3,0
        ori     r3,r3,MSR_ME
        andc    r0,r0,r3                /* Clears bit in (r4) */
        sync                            /* Some chip revs have problems here */
        mtmsr   r0                      /* Update machine state */
        blr

/*
 * Create a kernel thread
 *   kernel_thread(fn, arg, flags)
 */
_GLOBAL(kernel_thread)
	mr	r6,r3		/* function */
	ori	r3,r5,CLONE_VM	/* flags */
	li	r0,__NR_clone
	sc
	cmpi	0,r3,0		/* parent or child? */
	bnelr			/* return if parent */
	li	r0,0		/* clear out p->tss.regs */
	stw	r0,TSS+PT_REGS(r2)	/* since we don't have user ctx */
	mtlr	r6              /* fn addr in lr */
	mr	r3,r4	        /* load arg and call fn */
	blrl
	li	r0,__NR_exit	/* exit after child exits */
        li	r3,0
	sc

#define SYSCALL(name) \
_GLOBAL(name) \
	li	r0,__NR_##name; \
	sc; \
	bnslr; \
	lis	r4,errno@ha; \
	stw	r3,errno@l(r4); \
	li	r3,-1; \
	blr

#define __NR__exit __NR_exit

SYSCALL(idle)
SYSCALL(sync)
SYSCALL(setsid)
SYSCALL(write)
SYSCALL(dup)
SYSCALL(execve)
SYSCALL(open)
SYSCALL(close)
SYSCALL(waitpid)
SYSCALL(fork)
SYSCALL(delete_module)
SYSCALL(_exit)
SYSCALL(lseek)
SYSCALL(read)

/* Why isn't this a) automatic, b) written in 'C'? */	
	.data
	.align 4
	.globl	sys_call_table
sys_call_table:
	.long sys_ni_syscall	/* 0  -  old "setup()" system call */
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink	/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_lchown
	.long sys_ni_syscall			/* old break syscall holder */
	.long sys_stat
	.long sys_lseek
	.long sys_getpid	/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid
	.long sys_getuid
	.long sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_fstat
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_ni_syscall			/* old stty syscall holder */
	.long sys_ni_syscall			/* old gtty syscall holder */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall	/* 35 */	/* old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall			/* old prof syscall holder */
	.long sys_brk		/* 45 */
	.long sys_setgid
	.long sys_getgid
	.long sys_signal
	.long sys_geteuid
	.long sys_getegid	/* 50 */
	.long sys_acct
	.long sys_umount			/* recycled never used phys() */
	.long sys_ni_syscall			/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall			/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall			/* old ulimit syscall holder */
	.long sys_olduname
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid	/* 70 */
	.long sys_setregid
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups	/* 80 */
	.long sys_setgroups
	.long ppc_select
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long old_readdir
	.long sys_mmap		/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown	/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall			/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs	/* 100 */
	.long sys_ioperm
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_uname
	.long sys_iopl		/* 110 */
	.long sys_vhangup
	.long sys_idle
	.long sys_vm86
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_modify_ldt
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_create_module
	.long sys_init_module
	.long sys_delete_module
	.long sys_get_kernel_syms	/* 130 */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long sys_ni_syscall	/* for afs_syscall */
	.long sys_setfsuid
	.long sys_setfsgid
	.long sys_llseek	/* 140 */
        .long sys_getdents
	.long ppc_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam	/* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid
	.long sys_getresuid	/* 165 */
	.long sys_query_module
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid
	.long sys_getresgid	/* 170 */
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	
	.long sys_rt_sigpending	/* 175 */
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread
	.long sys_pwrite	/* 180 */
	.long sys_chown
	.long sys_getcwd
	.long sys_capget
	.long sys_capset
	.long sys_sigaltstack	/* 185 */
	.long sys_sendfile
	.long sys_ni_syscall		/* streams1 */
	.long sys_ni_syscall		/* streams2 */
	.long sys_vfork
	.long sys_ni_syscall		/* 190 */	/* MacOnLinux - old */
	.long sys_ni_syscall		/* 191 */	/* Unused */
	.long sys_ni_syscall		/* 192 - reserved - mmap2 */
	.long sys_ni_syscall		/* 193 - reserved - truncate64 */
	.long sys_ni_syscall		/* 194 - reserved - ftruncate64 */
	.long sys_ni_syscall		/* 195 - reserved - stat64 */
	.long sys_ni_syscall		/* 196 - reserved - lstat64 */
	.long sys_ni_syscall		/* 197 - reserved - fstat64 */
	.long sys_pciconfig_read	/* 198 */
	.long sys_pciconfig_write 	/* 199 */
	.long sys_pciconfig_iobase 	/* 200 */
	.long sys_ni_syscall		/* 201 - reserved - MacOnLinux - new */
	.rept NR_syscalls-201
		.long sys_ni_syscall
	.endr
