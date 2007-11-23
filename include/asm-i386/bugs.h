/*
 *  include/asm-i386/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 */

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#include <linux/config.h>

#define CONFIG_BUGi386

static void no_halt(char *s, int *ints)
{
	hlt_works_ok = 0;
}

static void no_387(char *s, int *ints)
{
	hard_math = 0;
	__asm__("movl %%cr0,%%eax\n\t"
		"orl $0xE,%%eax\n\t"
		"movl %%eax,%%cr0\n\t" : : : "ax");
}

static char fpu_error = 0;

static void copro_timeout(void)
{
	fpu_error = 1;
	timer_table[COPRO_TIMER].expires = jiffies+100;
	timer_active |= 1<<COPRO_TIMER;
	printk("387 failed: trying to reset\n");
	send_sig(SIGFPE, last_task_used_math, 1);
	outb_p(0,0xf1);
	outb_p(0,0xf0);
}

static void check_fpu(void)
{
	static double x = 4195835.0;
	static double y = 3145727.0;
	unsigned short control_word;

	if (!hard_math) {
#ifndef CONFIG_MATH_EMULATION
		printk("No coprocessor found and no math emulation present.\n");
		printk("Giving up.\n");
		for (;;) ;
#endif
		return;
	}
	/*
	 * check if exception 16 works correctly.. This is truly evil
	 * code: it disables the high 8 interrupts to make sure that
	 * the irq13 doesn't happen. But as this will lead to a lockup
	 * if no exception16 arrives, it depends on the fact that the
	 * high 8 interrupts will be re-enabled by the next timer tick.
	 * So the irq13 will happen eventually, but the exception 16
	 * should get there first..
	 */
	printk("Checking 386/387 coupling... ");
	timer_table[COPRO_TIMER].expires = jiffies+50;
	timer_table[COPRO_TIMER].fn = copro_timeout;
	timer_active |= 1<<COPRO_TIMER;
	__asm__("clts ; fninit ; fnstcw %0 ; fwait":"=m" (*&control_word));
	control_word &= 0xffc0;
	__asm__("fldcw %0 ; fwait": :"m" (*&control_word));
	outb_p(inb_p(0x21) | (1 << 2), 0x21);
	__asm__("fldz ; fld1 ; fdiv %st,%st(1) ; fwait");
	timer_active &= ~(1<<COPRO_TIMER);
	if (fpu_error)
		return;
	if (!ignore_irq13) {
		printk("Ok, fpu using old IRQ13 error reporting\n");
		return;
	}
	__asm__("fninit\n\t"
		"fldl %1\n\t"
		"fdivl %2\n\t"
		"fmull %2\n\t"
		"fldl %1\n\t"
		"fsubp %%st,%%st(1)\n\t"
		"fistpl %0\n\t"
		"fwait\n\t"
		"fninit"
		: "=m" (*&fdiv_bug)
		: "m" (*&x), "m" (*&y));
	if (!fdiv_bug) {
		printk("Ok, fpu using exception 16 error reporting.\n");
		return;

	}
	printk("Hmm, FDIV bug i%c86 system\n", '0'+x86);
}

static void check_hlt(void)
{
	printk("Checking 'hlt' instruction... ");
	if (!hlt_works_ok) {
		printk("disabled\n");
		return;
	}
	__asm__ __volatile__("hlt ; hlt ; hlt ; hlt");
	printk("Ok.\n");
}

static void check_tlb(void)
{
#ifndef CONFIG_M386
	/*
	 * The 386 chips don't support TLB finegrained invalidation.
	 * They will fault when they hit a invlpg instruction.
	 */
	if (x86 == 3) {
		printk("CPU is a 386 and this kernel was compiled for 486 or better.\n");
		printk("Giving up.\n");
		for (;;) ;
	}
#endif
}

/*
 * All current models of Pentium and Pentium with MMX technology CPUs
 * have the F0 0F bug, which lets nonpriviledged users lock up the system:
 */
extern int pentium_f00f_bug;
extern void trap_init_f00f_bug(void);

static void check_pentium_f00f(void)
{
	/*
	 * Pentium and Pentium MMX
	 */
	pentium_f00f_bug = 0;
	if (x86==5 && !memcmp(x86_vendor_id, "GenuineIntel", 12)) {
		printk(KERN_INFO "Intel Pentium with F0 0F bug - workaround enabled.\n");
		pentium_f00f_bug = 1;
		trap_init_f00f_bug();
	}
}

/*
 *      B step AMD K6 before B 9730xxxx have hardware bugs that can cause
 *      misexecution of code under Linux. Owners of such processors should
 *      contact AMD for precise details and a (free) CPU exchange.
 *
 *      See     http://www.chorus.com/~poulot/k6bug.html
 *              http://www.amd.com/K6/k6docs/revgd.html
 *
 *      The following test is erm... interesting. AMD neglected to up
 *      the chip stepping when fixing the bug but they also tweaked some
 *      performance at the same time...
 */
 
extern void vide(void);
__asm__(".align 4\nvide: ret");

static void check_k6_bug(void)
{

        if ((strcmp(x86_vendor_id, "AuthenticAMD") == 0) &&
            (x86_model == 6) && (x86_mask == 1))
        {
		int n;
                void (*f_vide)(void);
                unsigned long d, d2;

                printk(KERN_INFO "AMD K6 stepping B detected - ");

#define K6_BUG_LOOP 1000000

                /*
                 * It looks like AMD fixed the 2.6.2 bug and improved indirect 
                 * calls at the same time.
                 */

                n = K6_BUG_LOOP;
                f_vide = vide;
                __asm__ ("rdtsc" : "=a" (d));
                while (n--) 
                        f_vide();
                __asm__ ("rdtsc" : "=a" (d2));
                d = d2-d;

                if (d > 20*K6_BUG_LOOP) {
                        printk("system stability may be impaired when more than 32 MB are used.\n");
		}
                else 
                        printk("probably OK (after B9730xxxx).\n");
        }
}

/* Cyrix stuff from this point on */

/* Cyrix 5/2 test (return 0x200 if it's a Cyrix) */
static inline int test_cyrix_52div(void)
{
	int test;

	__asm__ __volatile__("xor %%eax,%%eax\n\t"
	     "sahf\n\t"
	     "movb $5,%%al\n\t"
	     "movb $2,%%bl\n\t"
	     "div %%bl\n\t"
	     "lahf\n\t"
	     "andl $0xff00,%%eax": "=eax" (test) : : "bx");

	return test;
}

/* test for CCR3 bit 7 r/w */
static char test_cyrix_cr3rw(void)
{
	char temp, test;
	
	temp = getCx86(CX86_CCR3);	/* get current CCR3 value */
	setCx86(CX86_CCR3, temp ^ 0x80); /* toggle test bit and write */
	getCx86(0xc0);			/* dummy to change bus */
	test = temp - getCx86(CX86_CCR3);	/* != 0 if ccr3 r/w */
	setCx86(CX86_CCR3, temp);	/* return CCR3 to original value */
	
	return test;
}

/* redo the cpuid test in head.S, so that those 6x86(L) now get
   detected properly (0 == no cpuid) */
static inline int test_cpuid(void)
{
	int test;
	
	__asm__("pushfl\n\t"
	     "popl %%eax\n\t"
	     "movl %%eax,%%ecx\n\t"
	     "xorl $0x200000,%%eax\n\t"
	     "pushl %%eax\n\t"
	     "popfl\n\t"
	     "pushfl\n\t"
	     "popl %%eax\n\t"
	     "xorl %%ecx,%%eax\n\t"
	     "pushl %%ecx\n\t"
	     "popfl" : "=eax" (test) : : "cx");

	return test;
}

/* All Cyrix 6x86 and 6x86L need the SLOP bit reset so that the udelay loop
 * calibration works well.
 * This routine must be called with MAPEN enabled, otherwise we don't
 * have access to CCR5.
 */
 
static void check_6x86_slop(void)
{
	if (x86_model == 2)	/* if 6x86 or 6x86L */
		setCx86(CX86_CCR5, getCx86(CX86_CCR5) & 0xfd); /* reset SLOP */
}

/* Cyrix CPUs without cpuid or with cpuid not yet enabled can be detected
 * by the fact that they preserve the flags across the division of 5/2.
 * PII and PPro exhibit this behavior too, but they have cpuid available.
 */
 
static void check_cyrix_various(void)
{
	if ((x86 == 4) && (test_cyrix_52div()==0x200)) 
	{
		/* if it's a Cyrix */
	    
		unsigned long flags;

		/* default to an "old" Cx486 */
		strcpy(x86_vendor_id, "CyrixInstead");
		x86_model = -1;
		x86_mask = 0;
	    
		/* Disable interrupts */
		save_flags(flags);
		cli();
			
		/* First check for very old CX486 models */
		/* that did not have DIR0/DIR1. */
		if (test_cyrix_cr3rw()) 
		{	/* if has DIR0/DIR1 */
		
			char ccr3;
			char dir0;
			x86_model = 0;

			/* Enable MAPEN */
			ccr3 = getCx86(CX86_CCR3);
			setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10);

			dir0 = getCx86(CX86_DIR0);
			if ((dir0 & 0xf0) == 0x30)	/* Use DIR0 to determine if this is a 6x86 class processor */
			{
				/* try enabling cpuid */
				setCx86(CX86_CCR4, getCx86(CX86_CCR4) | 0x80);
			}

			if (test_cpuid()) 
			{
				int eax, dummy;

				/* get processor info */
			
				cpuid(1, &eax, &dummy, &dummy,
				      &x86_capability);

				have_cpuid = 1;
				x86_model = (eax >> 4) & 0xf;
				x86 = (eax >> 8) & 0xf;
				check_6x86_slop();
			}
			/* disable MAPEN */
			setCx86(CX86_CCR3, ccr3);
		} /* endif has DIR0/DIR1 */
		sti();
		restore_flags(flags);	/* restore interrupt state */
	} /* endif it's a Cyrix */
}

/* Check various processor bugs */

static void check_bugs(void)
{
	check_cyrix_various();
	check_k6_bug();
	check_tlb();
	check_fpu();
	check_hlt();
	check_pentium_f00f();
	system_utsname.machine[1] = '0' + x86;
}
