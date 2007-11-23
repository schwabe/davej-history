/*
 *  linux/arch/i386/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>
#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/io.h>

/*
 * Tell us the machine setup..
 */
char hard_math = 0;		/* set by kernel/head.S */
char x86 = 0;			/* set by kernel/head.S to 3..6 */
char x86_model = 0;		/* set by kernel/head.S */
char x86_mask = 0;		/* set by kernel/head.S */
int x86_capability = 0;		/* set by kernel/head.S */
int x86_ext_capability = 0;	/* newer CPUs have this */
int fdiv_bug = 0;		/* set if Pentium(TM) with FP bug */
int pentium_f00f_bug = 0;	/* set if Pentium(TM) with F00F bug */
int have_cpuid = 0;             /* set if CPUID instruction works */
int ext_cpuid = 0;		/* if != 0, highest available CPUID value */

char x86_vendor_id[13] = "GenuineIntel";/* default */

static char Cx86_step[8] = "unknown";	/* stepping info for Cyrix CPUs */

static unsigned char Cx86_mult = 0;	/* clock multiplier for Cyrix CPUs */

static const char *x86_clkmult[] = {
	"unknown", "1x", "1.5x", "2x", "2.5x", "3x", "3.5x", "4x", 
	"4.5x", "5x", "5.5x", "6x", "6.5x", "7x", "7.5x", "8x"
};

char ignore_irq13 = 0;		/* set if exception 16 works */
char wp_works_ok = -1;		/* set if paging hardware honours WP */
char hlt_works_ok = 1;		/* set if the "hlt" instruction works */

/*
 * Bus types ..
 */
int EISA_bus = 0;

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;
#ifdef CONFIG_APM
struct apm_bios_info apm_bios_info;
#endif

unsigned char aux_device_present;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern int root_mountflags;
extern int _etext, _edata, _end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))
#ifndef STANDARD_MEMORY_BIOS_CALL
#define ALT_MEM_K (*(unsigned long *) (PARAM+0x1e0))
#endif
#define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+0x40))
#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
#define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define KERNEL_START (*(unsigned long *) (PARAM+0x214))
#define INITRD_START (*(unsigned long *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))
#define COMMAND_LINE ((char *) (PARAM+2048))
#define COMMAND_LINE_SIZE 256

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;
#ifndef STANDARD_MEMORY_BIOS_CALL
	unsigned long memory_alt_end;
#endif
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;
	static unsigned char smptrap=0;

	if(smptrap==1)
	{
		return;
	}
	smptrap=1;

	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);
	drive_info = DRIVE_INFO;
	screen_info = SCREEN_INFO;
#ifdef CONFIG_APM
	apm_bios_info = APM_BIOS_INFO;
#endif
	aux_device_present = AUX_DEVICE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
#ifndef STANDARD_MEMORY_BIOS_CALL
	memory_alt_end = (1<<20) + (ALT_MEM_K<<10);
	if (memory_alt_end > memory_end) {
	    printk("Memory: sized by int13 0e801h\n");
	    memory_end = memory_alt_end;
	}
	else
	    printk("Memory: sized by int13 088h\n");
#endif
	memory_end &= PAGE_MASK;
#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
#ifdef CONFIG_MAX_16M
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
#endif

	/*
	 *	The CONFIG_MAX_MEMSIZE sanity checker.
	 */

	if (memory_end > (CONFIG_MAX_MEMSIZE-128)*1024*1024)
	{
		memory_end = (CONFIG_MAX_MEMSIZE-128)*1024*1024;
		printk(KERN_WARNING "ONLY %dMB RAM will be used, see Documentation/more-than-900MB-RAM.txt!.\n", CONFIG_MAX_MEMSIZE-128);
		udelay(3*1000*1000);
	}

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	memory_start = (unsigned long) &_end;
	init_task.mm->start_code = TASK_SIZE;
	init_task.mm->end_code = TASK_SIZE + (unsigned long) &_etext;
	init_task.mm->end_data = TASK_SIZE + (unsigned long) &_edata;
	init_task.mm->brk = TASK_SIZE + (unsigned long) &_end;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		/*
		 * "mem=nopentium" disables the 4MB page tables.
		 * "mem=XXX[kKmM]" overrides the BIOS-reported
		 * memory size
		 */
		if (c == ' ' && *(const unsigned long *)from == *(const unsigned long *)"mem=") {
			if (to != command_line) to--;
			if (!memcmp(from+4, "nopentium", 9)) {
				from += 9+4;
				x86_capability &= ~8;
			} else {
				memory_end = simple_strtoul(from+4, &from, 0);
				if ( *from == 'K' || *from == 'k' ) {
					memory_end = memory_end << 10;
					from++;
				} else if ( *from == 'M' || *from == 'm' ) {
					memory_end = memory_end << 20;
					from++;
				}
			}
		}
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
	*memory_start_p = memory_start;
	*memory_end_p = memory_end;

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE) {
		initrd_start = INITRD_START;
		initrd_end = INITRD_START+INITRD_SIZE;
		if (initrd_end > memory_end) {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    initrd_end,memory_end);
			initrd_start = 0;
		}
	}
#endif

	/* request io space for devices used on all i[345]86 PC'S */
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x20,"dma page reg");
	request_region(0xc0,0x20,"dma2");
	request_region(0xf0,0x10,"npu");
}

static const char * IDTmodel(void)
/* Right now IDT has a single CPU model in production: the C6.
 * Adjust this when IDT/Centaur comes out with a new CPU model.
 * Stepping information is correctly reported in x86_mask.
 */
{
	static const char *model[] = {
		"C6", "C6-3D"
	};
	return model[0];
}

static const char * Cx86model(void)
/* We know our CPU is a Cyrix now (see bugs.h), so we can use the DIR0/DIR1
 * mechanism to figure out the model, bus clock multiplier and stepping.
 * For the newest CPUs (GXm and MXi) we use the Extended CPUID function.
 */
{
    unsigned char nr6x86 = 0;
    unsigned char cx_dir0 = 0;	/* Model and bus clock multiplier */
    unsigned char cx_dir1 = 0;	/* Stepping info */
    unsigned long flags;
    static const char *model[] = {
	"unknown", "Cx486", "5x86", "MediaGX", "6x86", "6x86L", "6x86MX",
	"M II"
    };

    if (x86_model == -1) {	/* is this an old Cx486 without DIR0/DIR1? */
	nr6x86 = 1;		/* Cx486 */
	Cx86_mult = 0;		/* unknown multiplier */
    }
    else {

	/* Get DIR0, DIR1 since all other Cyrix CPUs have them */

	save_flags(flags);
	cli();
	cx_dir0 = getCx86(CX86_DIR0);	/* we use the access macros */
	cx_dir1 = getCx86(CX86_DIR1);	/* defined in processor.h */
	restore_flags(flags);

	/* Now cook; the recipe is by Channing Corn, from Cyrix.
	 * We do the same thing for each generation: we work out
	 * the model, multiplier and stepping.
	 */

	if (cx_dir0 < 0x20) {
		nr6x86 = 1;				/* Cx486 */
		Cx86_mult = 0;				/* unknown multiplier */
		sprintf(Cx86_step, "%d.%d", (cx_dir1 >> 4) + 1, cx_dir1 & 0x0f);
	}

        if ((cx_dir0 > 0x20) && (cx_dir0 < 0x30)) {
		nr6x86 = 2;				/* 5x86 */
		Cx86_mult = ((cx_dir0 & 0x04) ? 5 : 3);	/* either 3x or 2x */
		sprintf(Cx86_step, "%d.%d", (cx_dir1 >> 4) + 1, cx_dir1 & 0x0f);
	}

        if ((cx_dir0 >= 0x30) && (cx_dir0 < 0x38)) {
		nr6x86 = ((x86_capability & (1 << 8)) ? 5 : 4);	/* 6x86(L) */
		Cx86_mult = ((cx_dir0 & 0x04) ? 5 : 3);	/* either 3x or 2x */
		sprintf(Cx86_step, "%d.%d", (cx_dir1 >> 3), cx_dir1 & 0x0f);
	}

        if ((cx_dir0 >= 0x40) && (cx_dir0 < 0x50)) {
	    if (x86 == 4) { /* MediaGX */
		nr6x86 = 3;
		Cx86_mult = ((cx_dir0 & 0x01) ? 5 : 7);	/* either 3x or 4x */
		switch (cx_dir1 >> 4) {
			case (0) :
			case (1) :
				sprintf(Cx86_step, "2.%d", cx_dir1 & 0x0f);
				break;
			case (2) :
				sprintf(Cx86_step, "1.%d", cx_dir1 & 0x0f);
				break;
                        default  :
				break;
                }
            } /* endif MediaGX */
	    if (x86 == 5) { /* GXm */
		    char GXm_mult[8] = {7,11,7,11,13,15,13,9}; /* 4 to 8 */
		    ext_cpuid = 0x80000005;	/* available */
		    Cx86_mult = GXm_mult[cx_dir0 & 0x0f];
		    sprintf(Cx86_step, "%d.%d", (cx_dir1 >> 4) - 1, cx_dir1 & 0x0f);
	    } /* endif GXm */
        }

        if ((cx_dir0 >= 0x50) && (cx_dir0 < 0x60)) {
		nr6x86 = ((cx_dir1 > 7) ? 7 : 6);	/* 6x86Mx or M II */
		Cx86_mult = (cx_dir0 & 0x07) + 2;	/* 2 to 5 in 0.5 steps */
		if (((cx_dir1 & 0x0f) > 4) || ((cx_dir1 >> 4) == 2)) cx_dir1 += 0x10;
		sprintf(Cx86_step, "%d.%d", (cx_dir1 >> 4) + 1, cx_dir1 & 0x0f);
	}
    }
    x86_mask = 1;	/* we don't use it, but has to be set to something */
    return model[nr6x86];
}

struct cpu_model_info {
	int cpu_x86;
	char *model_names[16];
};

static struct cpu_model_info amd_models[] = {
	{ 4,
	  { NULL, NULL, NULL, "DX/2", NULL, NULL, NULL, "DX/2-WB", "DX/4",
	    "DX/4-WB", NULL, NULL, NULL, NULL, "Am5x86-WT", "Am5x86-WB" }},
	{ 5,
	  { "K5/SSA5 (PR-75, PR-90, PR-100)"}},
};

static const char * AMDmodel(void)
{
	const char *p=NULL;
	int i;

	if ((x86_model == 0) || (x86 == 4)) {
		for (i=0; i<sizeof(amd_models)/sizeof(struct cpu_model_info); i++)
			if (amd_models[i].cpu_x86 == x86) {
				p = amd_models[i].model_names[(int)x86_model];
				break;
			}
	}
	else ext_cpuid = 0x80000005;		/* available */
	return p;
}

static struct cpu_model_info intel_models[] = {
	{ 4,
	  { "486 DX-25/33", "486 DX-50", "486 SX", "486 DX/2", "486 SL", 
	    "486 SX/2", NULL, "486 DX/2-WB", "486 DX/4", "486 DX/4-WB", NULL, 
	    NULL, NULL, NULL, NULL, NULL }},
	{ 5,
	  { "Pentium 60/66 A-step", "Pentium 60/66", "Pentium 75+",
	    "OverDrive PODP5V83", "Pentium MMX", NULL, NULL,
	    "Mobile Pentium 75+", "Mobile Pentium MMX", NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL }},
	{ 6,
	  { "Pentium Pro A-step", "Pentium Pro", NULL, "Pentium II (Klamath)", 
	    NULL, "Pentium II (Deschutes)", "Celeron (Mendocino)", NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL }},
};

static const char * Intelmodel(void)
{
	const char *p = "386 SX/DX";	/* default to a 386 */
	int i;

	/*
	 *	Old 486SX has no CPU ID. Set the model to 2 for this
	 *	case.
	 */

	if( x86==4 && x86_model == 0 && hard_math == 0)
		x86_model = 2;

	for (i=0; i<sizeof(intel_models)/sizeof(struct cpu_model_info); i++)
		if (intel_models[i].cpu_x86 == x86) {
			p = intel_models[i].model_names[(int)x86_model];
			break;
		}

	return p;
}

/* Recent Intel CPUs have an EEPROM and a ROM with CPU information. We'll use
 * this information in future versions of this code.
 * AMD and more recently Cyrix have decided to standardize on an extended
 * cpuid mechanism for their CPUs.
 */

static const char * get_cpu_mkt_name(void)
{
        static char mktbuf[48];
        int dummy;
	unsigned int *v;
	v = (unsigned int *) mktbuf;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);	/* name, flags */
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	cpuid(0x80000001, &dummy, &dummy, &dummy, &x86_ext_capability);
	return mktbuf;
}

static const char * getmodel(void)
/* Default is Intel. We disregard Nexgen processors. */
{
        const char *p = NULL;
	if      (strcmp(x86_vendor_id, "AuthenticAMD") == 0)	/* AuthenticAMD */
		p = AMDmodel();
	else if (strcmp(x86_vendor_id, "CyrixInstead") == 0)	/* CyrixInstead */
		p = Cx86model();
	else if (strcmp(x86_vendor_id, "CentaurHauls") == 0)	/* CentaurHauls */
		p = IDTmodel();
	/* This isnt quite right */
	else if (strcmp(x86_vendor_id, "UMC UMC UMC ") == 0)	/* UMC */
		p = Intelmodel();
	else /* default - this could be anyone */
		p = Intelmodel();
	if (ext_cpuid)
		return get_cpu_mkt_name();
        else
                return p;
}

int get_cpuinfo(char * buffer)
{
        int i, len = 0;
        static const char *x86_cap_flags[] = {
                "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
                "cx8", "apic", "10", "sep", "mtrr", "pge", "mca", "cmov",
                "16", "17", "18", "19", "20", "21", "22", "mmx",
                "24", "25", "26", "27", "28", "29", "30", "31"
        };
	static const char *x86_ext_cap_flags[] = {
		   "fpu","vme", "de",   "pse", "tsc", "msr",  "6",   "mce",
		   "cx8",  "9", "10", "syscr",  "12", "pge", "14",  "cmov",
		"fpcmov", "17", "psn",    "19",  "20",  "21", "22",   "mmx",
		  "emmx", "25", "26",    "27",  "28",  "29", "30", "3dnow"
	};

#ifdef __SMP__
        int n;

#define CD(X)		(cpu_data[n].X)
/* SMP has the wrong name for loops_per_sec */
#define loops_per_sec	udelay_val
#define CPUN n

        for ( n = 0 ; n < 32 ; n++ ) {
                if ( cpu_present_map & (1<<n) ) {
                        if (len) buffer[len++] = '\n';

#else
#define CD(X) (X)
#define CPUN 0
#endif

                        len += sprintf(buffer+len,"processor\t: %d\n"
                                       "cpu\t\t: %c86\n"
                                       "model\t\t: %s",
                                       CPUN,
                                       CD(x86)+'0',
                                       getmodel());
			len += sprintf(buffer+len,
                                       "\nvendor_id\t: %s\n",
                                       x86_vendor_id);

                        if (CD(x86_mask) || have_cpuid)
                                if ((strncmp(x86_vendor_id, "Au", 2) == 0)
					&& (x86_model >= 6)) {
					len += sprintf(buffer+len,
							"stepping\t: %c\n",
							x86_mask + 'A');
                                }
                                else if (strncmp(x86_vendor_id, "Cy", 2) == 0) {
					len += sprintf(buffer+len,
							"stepping\t: %s, core/bus clock ratio: %s\n",
							Cx86_step, x86_clkmult[Cx86_mult]);
                                }
                                else {
					len += sprintf(buffer+len,
						       "stepping\t: %d\n",
						       CD(x86_mask));
                                }
                        else
                                len += sprintf(buffer+len,
                                               "stepping\t: unknown\n");

                        len += sprintf(buffer+len,
                                       "fdiv_bug\t: %s\n"
                                       "hlt_bug\t\t: %s\n"
                                       "f00f_bug\t: %s\n"
                                       "fpu\t\t: %s\n"
                                       "fpu_exception\t: %s\n"
                                       "cpuid\t\t: %s\n"
                                       "wp\t\t: %s\n"
                                       "flags\t\t:",
                                       CD(fdiv_bug) ? "yes" : "no",
                                       CD(hlt_works_ok) ? "no" : "yes",
                                       pentium_f00f_bug ? "yes" : "no",
                                       CD(hard_math) ? "yes" : "no",
                                       (CD(hard_math) && ignore_irq13)
                                         ? "yes" : "no",
                                       CD(have_cpuid) ? "yes" : "no",
                                       CD(wp_works_ok) ? "yes" : "no");

                        for ( i = 0 ; i < 32 ; i++ ) {
                                if ( CD(x86_capability) & (1 << i) ) {
                                        len += sprintf(buffer+len, " %s",
                                                       x86_cap_flags[i]);
                                }
                                else if ( CD(x86_ext_capability) & (1 << i) ) {
                                        len += sprintf(buffer+len, " %s",
                                                       x86_ext_cap_flags[i]);
                                }
                        }
                        len += sprintf(buffer+len,
                                       "\nbogomips\t: %lu.%02lu\n",
                                       CD(loops_per_sec+2500)/500000,
                                       (CD(loops_per_sec+2500)/5000) % 100);
#ifdef __SMP__
                }
        }
#endif
        return len;
}
