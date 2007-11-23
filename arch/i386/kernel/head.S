/*
 *  linux/arch/i386/head.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 */

.text
#include <linux/config.h>
#include <linux/tasks.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <linux/config.h>
#include <asm/page.h>
#include <asm/pgtable.h>


#define CL_MAGIC_ADDR	0x90020
#define CL_MAGIC	0xA33F
#define CL_BASE_ADDR	0x90000
#define CL_OFFSET	0x90022

/*
 * swapper_pg_dir is the main page directory, address 0x00001000 (or at
 * address 0x00101000 for a compressed boot).
 */
ENTRY(stext)
ENTRY(_stext)
startup_32:
	cld
	movl $(KERNEL_DS),%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
#ifdef __SMP__
	orw  %bx,%bx
	jz  1f				/* Initial CPU cleans BSS */
/*
 *	Set up the stack
 */
	mov %ax,%ss
	xorl %eax,%eax
	movw %cx, %ax
	movl %eax,%esp
	pushl $0
	popfl
	jmp checkCPUtype
1:
	lss stack_start,%esp
#endif /* __SMP__ */
/*
 * Clear BSS first so that there are no surprises...
 */
	xorl %eax,%eax
	movl $ SYMBOL_NAME(_edata),%edi
	movl $ SYMBOL_NAME(_end),%ecx
	subl %edi,%ecx
	cld
	rep
	stosb
/*
 * start system 32-bit setup. We need to re-do some of the things done
 * in 16-bit mode for the "real" operations.
 */
	call setup_idt
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b
/*
 * Initialize eflags.  Some BIOS's leave bits like NT set.  This would
 * confuse the debugger if this code is traced.
 * XXX - best to initialize before switching to protected mode.
 */
	pushl $0
	popfl
/*
 * Copy bootup parameters out of the way. First 2kB of
 * _empty_zero_page is for boot parameters, second 2kB
 * is for the command line.
 */
	movl $0x90000,%esi
	movl $ SYMBOL_NAME(empty_zero_page),%edi
	movl $512,%ecx
	cld
	rep
	movsl
	xorl %eax,%eax
	movl $512,%ecx
	rep
	stosl
	cmpw $(CL_MAGIC),CL_MAGIC_ADDR
	jne 1f
	movl $ SYMBOL_NAME(empty_zero_page)+2048,%edi
	movzwl CL_OFFSET,%esi
	addl $(CL_BASE_ADDR),%esi
	movl $2048,%ecx
	rep
	movsb
1:
#ifdef __SMP__
checkCPUtype:
#endif

/* check Processor type: 386, 486, 6x86(L) or CPUID capable processor */
/*
 * XXX - this does a lot of unnecessary setup.  Alignment checks don't
 * apply at our cpl of 0 and the stack ought to be aligned already, and
 * we don't need to preserve eflags.
 */

	movl $3, SYMBOL_NAME(x86)
	pushfl			# push EFLAGS
	popl %eax		# get EFLAGS
	movl %eax,%ecx		# save original EFLAGS in ecx
	xorl $0x40000,%eax	# flip AC bit in EFLAGS
	pushl %eax		# copy to EFLAGS
	popfl			# set EFLAGS
	pushfl			# get new EFLAGS
	popl %eax		# put it in eax
	xorl %ecx,%eax		# change in flags
	andl $0x40000,%eax	# check if AC bit changed
	je is386
	movl $4,SYMBOL_NAME(x86)
	movl %ecx,%eax
	xorl $0x200000,%eax	# check ID flag
	pushl %eax
	popfl			# if we are on a straight 486DX, SX, or
	pushfl			# 487SX we can't change it
	popl %eax		# Also if we are on a Cyrix 6x86(L)
	xorl %ecx,%eax		# OTOH 6x86MXs and MIIs check OK
	andl $0x200000,%eax
	je is486x

isnew:	pushl %ecx		# restore original EFLAGS
	popfl
	incl SYMBOL_NAME(have_cpuid)	# we have CPUID
	/*
 	 *	Technically we should use CPUID 0 to see if we have CPUID 1!
	 */
	/* get processor type */
	movl $1, %eax		# Use the CPUID instruction to 
#ifdef GAS_KNOWS_CPUID
	cpuid			# check the processor type
#else
	.byte 0x0f, 0xa2	# check the processor type
#endif
	movb %al, %cl		# save reg for future use
	andb $0x0f,%ah		# mask processor family
	movb %ah,SYMBOL_NAME(x86)
	andb $0xf0, %al		# mask model
	shrb $4, %al
	movb %al,SYMBOL_NAME(x86_model)
	andb $0x0f, %cl		# mask mask revision
	movb %cl,SYMBOL_NAME(x86_mask)
	movl %edx,SYMBOL_NAME(x86_capability)
	/* get vendor info */
	xorl %eax, %eax			# call CPUID with 0 -> return vendor ID
#ifdef GAS_KNOWS_CPUID
	cpuid
#else
	.byte 0x0f, 0xa2		# CPUID
#endif
	movl %ebx,SYMBOL_NAME(x86_vendor_id)	# lo 4 chars
	movl %edx,SYMBOL_NAME(x86_vendor_id)+4	# next 4 chars
	movl %ecx,SYMBOL_NAME(x86_vendor_id)+8	# last 4 chars

	movl %cr0,%eax		# 486+
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl $0x50022,%eax	# set AM, WP, NE and MP
	jmp 2f

/* Now we test if we have a Cyrix 6x86(L). We didn't test before to avoid
 * clobbering the new BX chipset used with the Pentium II, which has a register
 * at the same addresses as those used to access the Cyrix special configuration
 * registers (CCRs).
 */
	/*
	 * A Cyrix/IBM 6x86(L) preserves flags after dividing 5 by 2
	 * (and it _must_ be 5 divided by 2) while other CPUs change
	 * them in undefined ways. We need to know this since we may
	 * need to enable the CPUID instruction at least.
	 * We couldn't use this test before since the PPro and PII behave
	 * like Cyrix chips in this respect.
	 */
is486x:	xor %ax,%ax
	sahf
	movb $5,%ax
	movb $2,%bx
	div %bl
	lahf
	cmpb $2,%ah
	jne ncyrix
	/*
	 * N.B. The pattern of accesses to 0x22 and 0x23 is *essential*
	 *      so do not try to "optimize" it! For the same reason we
	 *	do all this with interrupts off.
	 */
#define setCx86(reg, val) \
	movb reg,%ax;	\
	outb %ax,$0x22;	\
	movb val,%ax;	\
	outb %ax,$0x23

#define getCx86(reg) \
	movb reg,%ax;	\
	outb %ax,$0x22;	\
	inb $0x23,%ax

	cli
	getCx86($0xc3)		# get CCR3
	movb %ax,%cx		# Save old value
	movb %ax,%bx
	andb $0x0f,%bx		# Enable access to all config registers
	orb $0x10,%bx		# by setting bit 4
	setCx86($0xc3,%bx)

        getCx86($0xfe)          # DIR0 : let's check this is a 6x86(L)
        andb $0xf0,%ax		# should be 3xh
	cmpb $0x30,%ax		# 
	jne n6x86

	getCx86($0xe8)		# now we can get CCR4
	orb $0x80,%ax		# and set bit 7 (CPUIDEN)
	movb %ax,%bx		# to enable CPUID execution
	setCx86($0xe8,%bx)

        getCx86($0xe9)          # CCR5 : we reset the SLOP bit
        andb $0xfd,%ax		# so that udelay calculation
        movb %ax,%bx		# is correct on 6x86(L) CPUs
        setCx86($0xe9,%bx)
	setCx86($0xc3,%cx)	# Restore old CCR3
	sti
	jmp isnew		# We enabled CPUID now

n6x86:	setCx86($0xc3,%cx)	# Restore old CCR3
	sti
ncyrix:	pushl %ecx		# restore original EFLAGS
	popfl
	movl %cr0,%eax		# 486
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl $0x50022,%eax	# set AM, WP, NE and MP
	jmp 2f
is386:	pushl %ecx		# restore original EFLAGS
	popfl
	movl %cr0,%eax		# 386
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl $2,%eax		# set MP
2:	movl %eax,%cr0
	call check_x87
#ifdef __SMP__
	movb ready,%al
	orb %al,%al
	jz 3f	
	movl $ SYMBOL_NAME(swapper_pg_dir), %eax
	movl %eax, %cr3
#ifdef GAS_KNOWS_CR4
	movl %cr4,%eax
	orl $16,%eax
	movl %eax,%cr4
#else
	.byte 0x0f,0x20,0xe0
	orl $16,%eax
	.byte 0x0f,0x22,0xe0
#endif
	movl %cr0, %eax
	orl $0x80000000, %eax
	movl %eax, %cr0
	jmp 4f
#endif
3:
	call setup_paging
#ifdef __SMP__
	incb ready
#endif
4:
	lgdt gdt_descr
	lidt idt_descr
	ljmp $(KERNEL_CS),$1f
1:	movl $(KERNEL_DS),%eax	# reload all the segment registers
	mov %ax,%ds		# after changing gdt.
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
#ifdef __SMP__
	movl $(KERNEL_DS), %eax
	mov  %ax,%ss		# Reload the stack pointer (segment only)
#else
	lss stack_start,%esp	# Load processor stack
#endif
	xorl %eax,%eax
	lldt %ax
	pushl %eax		# These are the parameters to main :-)
	pushl %eax
	pushl %eax
	cld			# gcc2 wants the direction flag cleared at all times
	call SYMBOL_NAME(start_kernel)
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

#ifdef __SMP__
ready:	.byte 0
#endif

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	movb $0,SYMBOL_NAME(hard_math)
	clts
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f
	movl %cr0,%eax		/* no coprocessor: have to set bits */
	xorl $4,%eax		/* set EM */
	movl %eax,%cr0
	ret
	ALIGN
1:	movb $1,SYMBOL_NAME(hard_math)
	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It doesn't actually load
 *  idt - that can be done only after paging has been enabled
 *  and the kernel moved to PAGE_OFFSET. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok.
 */
setup_idt:
	lea ignore_int,%edx
	movl $(KERNEL_CS << 16),%eax
	movw %dx,%ax		/* selector = 0x0010 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea SYMBOL_NAME(idt),%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	ret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 4MB.  The rest are initialized later.
 *
 * (ref: added support for up to 32mb, 17Apr92)  -- Rik Faith
 * (ref: update, 25Sept92)  -- croutons@crunchy.uucp 
 * (ref: 92.10.11 - Linus Torvalds. Corrected 16M limit - no upper memory limit)
 */
	ALIGN
setup_paging:
	movl $1024*2,%ecx		/* 2 pages - swapper_pg_dir+1 page table */
	xorl %eax,%eax
	movl $ SYMBOL_NAME(swapper_pg_dir),%edi	/* swapper_pg_dir is at 0x1000 */
	cld;rep;stosl
/* Identity-map the kernel in low 4MB memory for ease of transition */
/* set present bit/user r/w */
	movl $ SYMBOL_NAME(pg0)+7,SYMBOL_NAME(swapper_pg_dir)
/* But the real place is at PAGE_OFFSET */
/* set present bit/user r/w */
	movl $ SYMBOL_NAME(pg0)+7,SYMBOL_NAME(swapper_pg_dir)+__USER_PGD_PTRS*4
	movl $ SYMBOL_NAME(pg0)+4092,%edi
	movl $0x03ff007,%eax		/*  4Mb - 4096 + 7 (r/w user,p) */
	std
1:	stosl			/* fill the page backwards - more efficient :-) */
	subl $0x1000,%eax
	jge 1b
	cld
	movl $ SYMBOL_NAME(swapper_pg_dir),%eax
	movl %eax,%cr3			/* cr3 - page directory start */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes the prefetch-queue */

/*
 * page 0 is made non-existent, so that kernel NULL pointer references get
 * caught. Thus the swapper page directory has been moved to 0x1000
 *
 * XXX Actually, the swapper page directory is at 0x1000 plus 1 megabyte,
 * with the introduction of the compressed boot code.  Theoretically,
 * the original design of overlaying the startup code with the swapper
 * page directory is still possible --- it would reduce the size of the kernel
 * by 2-3k.  This would be a good thing to do at some point.....
 */
.org 0x1000
ENTRY(swapper_pg_dir)
/*
 * The page tables are initialized to only 4MB here - the final page
 * tables are set up later depending on memory size.
 */
.org 0x2000
ENTRY(pg0)

.org 0x3000
ENTRY(empty_bad_page)

.org 0x4000
ENTRY(empty_bad_page_table)

.org 0x5000
ENTRY(empty_zero_page)

.org 0x6000

stack_start:
	.long SYMBOL_NAME(init_user_stack)+4096
	.long KERNEL_DS

/* NOTE: keep the idt short behind the above '.org 0x6000'
	 It must fit completely within _one_ page */
ENTRY(idt)
	.fill 256,8,0		# idt is uninitialized

/* This is the default interrupt "handler" :-) */
int_msg:
	.asciz "Unknown interrupt\n"
	ALIGN
ignore_int:
	cld
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $(KERNEL_DS),%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call SYMBOL_NAME(printk)
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

/*
 * The interrupt descriptor table has room for 256 idt's
 */
	ALIGN
.word 0
idt_descr:
	.word 256*8-1		# idt contains 256 entries
	.long __PAGE_OFFSET+SYMBOL_NAME(idt)

	ALIGN
.word 0
gdt_descr:
#ifdef CONFIG_APM
	.word (11+2*NR_TASKS)*8-1
#else
	.word (8+2*NR_TASKS)*8-1
#endif
	.long __PAGE_OFFSET+SYMBOL_NAME(gdt)

/*
 * This gdt setup gives the kernel a 1GB address space at virtual
 * address PAGE_OFFSET - space enough for expansion, I hope.
 */

#define upper_seg(type,dpl,base,limit) \
        ((base) & 0xff000000) | \
        (((base) & 0x00ff0000)>>16) | \
        (((limit)>>12) & 0xf0000) | \
        ((dpl)<<13) | \
        (0x00c09000) | \
        ((type)<<8)

#define lower_seg(type,dpl,base,limit) \
        (((base) & 0x0000ffff)<<16) | \
        (((limit)>>12) & 0x0ffff)

#define x86_seg(type,dpl,base,limit) \
	.long lower_seg(type,dpl,base,limit); \
	.long upper_seg(type,dpl,base,limit)

ENTRY(gdt)
	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x0000000000000000	/* not used */

	/* 0x10 kernel 1GB code at 0xC0000000: */
	x86_seg(0xa,0,__PAGE_OFFSET,0xffffffff-__PAGE_OFFSET)

	/* 0x18 kernel 1GB data at 0xC0000000: */
	x86_seg(0x2,0,__PAGE_OFFSET,0xffffffff-__PAGE_OFFSET)

	/* 0x23 user 3GB code at 0x00000000: */
        x86_seg(0xa,3,0,__PAGE_OFFSET-1)

	/* 0x2b user 3GB data at 0x00000000: */
        x86_seg(0x2,3,0,__PAGE_OFFSET-1)

	.quad 0x0000000000000000	/* not used */
	.quad 0x0000000000000000	/* not used */
	.fill 2*NR_TASKS,8,0		/* space for LDT's and TSS's etc */
#ifdef CONFIG_APM
	.quad 0x00c09a0000000000	/* APM CS    code */
	.quad 0x00809a0000000000	/* APM CS 16 code (16 bit) */
	.quad 0x00c0920000000000	/* APM DS    data */
#endif
