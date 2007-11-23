/*
 *  linux/arch/arm/kernel/setup.c
 *
 *  Copyright (C) 1995-1999 Russell King
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/blk.h>
#include <linux/console.h>
#include <linux/init.h>

#include <asm/elf.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/procinfo.h>
#include <asm/setup.h>
#include <asm/system.h>

#ifndef MEM_SIZE
#define MEM_SIZE	(16*1024*1024)
#endif

#ifndef CONFIG_CMDLINE
#define CONFIG_CMDLINE ""
#endif

extern void reboot_setup(char *str, int *ints);
extern void disable_hlt(void);
extern int root_mountflags;
extern int _stext, _text, _etext, _edata, _end;

unsigned int processor_id;
unsigned int __machine_arch_type;
unsigned int vram_size;
unsigned int system_rev;
unsigned int system_serial_low;
unsigned int system_serial_high;
unsigned int elf_hwcap;

#ifdef CONFIG_ARCH_ACORN
unsigned int memc_ctrl_reg;
unsigned int number_mfm_drives;
#endif

static unsigned long mem_end;

struct machine_desc {
	const char	*name;		/* architecture name	*/
	unsigned int	param_offset;	/* parameter page	*/
	unsigned int	video_start;	/* start of video RAM	*/
	unsigned int	video_end;	/* end of video RAM	*/
	unsigned int	reserve_lp0 :1;	/* never has lp0	*/
	unsigned int	reserve_lp1 :1;	/* never has lp1	*/
	unsigned int	reserve_lp2 :1;	/* never has lp2	*/
	unsigned int	broken_hlt  :1;	/* hlt is broken	*/
	unsigned int	soft_reboot :1;	/* soft reboot		*/
	void		(*fixup)(struct machine_desc *,
				 struct param_struct *, char **);
};

struct processor processor;

struct drive_info_struct { char dummy[32]; } drive_info;

struct screen_info screen_info = {
 orig_video_lines:	30,
 orig_video_cols:	80,
 orig_video_mode:	0,
 orig_video_ega_bx:	0,
 orig_video_isVGA:	1,
 orig_video_points:	8
};

unsigned char aux_device_present;
char elf_platform[ELF_PLATFORM_SIZE];
char saved_command_line[COMMAND_LINE_SIZE];

static struct proc_info_item proc_info;
static const char *machine_name;
static char command_line[COMMAND_LINE_SIZE] = { 0, };

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;
static union { char c[4]; unsigned long l; } endian_test __initdata = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

static void __init setup_processor(void)
{
	extern struct proc_info_list __proc_info_begin, __proc_info_end;
	struct proc_info_list *list;

	/*
	 * locate processor in the list of supported processor
	 * types.  The linker builds this table for us from the
	 * entries in arch/arm/mm/proc-*.S
	 */
	for (list = &__proc_info_begin; list < &__proc_info_end ; list++)
		if ((processor_id & list->cpu_mask) == list->cpu_val)
			break;

	/*
	 * If processor type is unrecognised, then we
	 * can do nothing...
	 */
	if (list >= &__proc_info_end) {
		printk("CPU configuration botched (ID %08x), unable "
		       "to continue.\n", processor_id);
		while (1);
	}

	proc_info = *list->info;

	processor = *list->proc;

	printk("Processor: %s %s revision %d\n",
	       proc_info.manufacturer, proc_info.cpu_name,
	       (int)processor_id & 15);

	sprintf(system_utsname.machine, "%s%c", list->arch_name, ENDIANNESS);
	sprintf(elf_platform, "%s%c", list->elf_name, ENDIANNESS);
	elf_hwcap = list->elf_hwcap;

	processor._proc_init();
}

static unsigned long __init memparse(char *ptr, char **retptr)
{
	unsigned long ret = simple_strtoul(ptr, retptr, 0);

	switch (**retptr) {
	case 'M':
	case 'm':
		ret <<= 10;
	case 'K':
	case 'k':
		ret <<= 10;
		(*retptr)++;
	default:
		break;
	}
	return ret;
}

/*
 * Initial parsing of the command line.  We need to pick out the
 * memory size.  We look for mem=size, where size is "size[KkMm]"
 */
static void __init
parse_cmdline(char **cmdline_p, char *from)
{
	char c = ' ', *to = command_line;
	int usermem = 0, len = 0;

	for (;;) {
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			unsigned long size, start;

			if (to != command_line)
				to -= 1;

			start = PAGE_OFFSET;
			size  = memparse(from + 4, &from);
			if (*from == '@')
				memparse(from + 1, &from);

			if (usermem == 0) {
				usermem = 1;
				mem_end = start + size;
			}
		}
		c = *from++;
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*to++ = c;
	}
	*to = '\0';
	*cmdline_p = command_line;

	/* remove trailing spaces */
	while (*--to == ' ' && to != command_line)
		*to = '\0';
}

static void __init
setup_ramdisk(int doload, int prompt, int image_start, unsigned int rd_sz)
{
#ifdef CONFIG_BLK_DEV_RAM
	extern int rd_doload, rd_prompt, rd_image_start, rd_size;

	rd_image_start = image_start;
	rd_prompt = prompt;
	rd_doload = doload;

	if (rd_sz)
		rd_size = rd_sz;
#endif
}

/*
 * initial ram disk
 */
static void __init setup_initrd(unsigned int start, unsigned int size)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (start == 0)
		size = 0;
	initrd_start = start;
	initrd_end   = start + size;
#endif
}

/*
 * Initialise memory.
 */
static void __init setup_bootmem(void)
{
	unsigned long mem_start;

	mem_start = (unsigned long)&_end;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_end > mem_end) {
		printk ("initrd extends beyond end of memory "
			"(0x%08lx > 0x%08lx) - disabling initrd\n",
			initrd_end, mem_end);
		initrd_start = 0;
	}
#endif
}

/*
 * Architecture specific fixups.  This is where any
 * parameters in the params struct are fixed up, or
 * any additional architecture specific information
 * is pulled from the params struct.
 */
static void __init
fixup_acorn(struct machine_desc *desc, struct param_struct *params,
	    char **cmdline)
{
#ifdef CONFIG_ARCH_ACORN
	if (machine_is_riscpc()) {
		/*
		 * RiscPC can't handle half-word loads and stores
		 */
		elf_hwcap &= ~HWCAP_HALF;

		switch (params->u1.s.pages_in_vram) {
		case 512:
			vram_size += PAGE_SIZE * 256;
		case 256:
			vram_size += PAGE_SIZE * 256;
		default:
			break;
		}
	}
	memc_ctrl_reg	  = params->u1.s.memc_control_reg;
	number_mfm_drives = (params->u1.s.adfsdrives >> 3) & 3;
#endif
}

static void __init
fixup_ebsa285(struct machine_desc *desc, struct param_struct *params,
	      char **cmdline)
{
	ORIG_X		 = params->u1.s.video_x;
	ORIG_Y		 = params->u1.s.video_y;
	ORIG_VIDEO_COLS  = params->u1.s.video_num_cols;
	ORIG_VIDEO_LINES = params->u1.s.video_num_rows;
}

/*
 * Older NeTTroms either do not provide a parameters
 * page, or they don't supply correct information in
 * the parameter page.
 */
static void __init
fixup_netwinder(struct machine_desc *desc, struct param_struct *params,
		char **cmdline)
{
	if (params->u1.s.nr_pages != 0x02000 &&
	    params->u1.s.nr_pages != 0x04000 &&
	    params->u1.s.nr_pages != 0x08000 &&
	    params->u1.s.nr_pages != 0x10000) {
		printk(KERN_WARNING "Warning: bad NeTTrom parameters "
		       "detected, using defaults\n");

		params->u1.s.nr_pages = 0x2000;	/* 32MB */
		params->u1.s.ramdisk_size = 0;
		params->u1.s.flags = FLAG_READONLY;
		params->u1.s.initrd_start = 0;
		params->u1.s.initrd_size = 0;
		params->u1.s.rd_start = 0;
	}
}

/*
 * CATS uses soft-reboot by default, since
 * hard reboots fail on early boards.
 */
static void __init
fixup_cats(struct machine_desc *desc, struct param_struct *params,
	   char **cmdline)
{
	ORIG_VIDEO_LINES  = 25;
	ORIG_VIDEO_POINTS = 16;
	ORIG_Y = 24;
}

#define NO_PARAMS	0
#define NO_VIDEO	0, 0

/*
 * This is the list of all architectures supported by
 * this kernel.  This should be integrated with the list
 * in head-armv.S.
 */
static struct machine_desc machine_desc[] __initdata = {
	{ "EBSA110",		/* RMK			*/
		0x00000400,
		NO_VIDEO,
		1, 0, 1, 1, 1,
		NULL
	}, { "Acorn-RiscPC",	/* RMK			*/
		0x10000100,
		NO_VIDEO,
		1, 1, 0, 0, 0,
		fixup_acorn
	}, { "unknown",
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "FTV/PCI",		/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "EBSA285",		/* RMK			*/
		0x00000100,
		0x000a0000, 0x000bffff,
		0, 0, 0, 0, 0,
		fixup_ebsa285
	}, { "Rebel-NetWinder",	/* RMK			*/
		0x00000100,
		0x000a0000, 0x000bffff,
		1, 0, 1, 0, 0,
		fixup_netwinder
	}, { "Chalice-CATS",	/* Philip Blundell	*/
		NO_PARAMS,
		0x000a0000, 0x000bffff,
		0, 0, 0, 0, 1,
		fixup_cats
	}, { "unknown-TBOX",	/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "co-EBSA285",	/* Mark van Doesburg	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "CL-PS7110",	/* Werner Almesberger	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "Acorn-Archimedes",/* RMK/DAG		*/
		0x0207c000,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_acorn
	}, { "Acorn-A5000",	/* RMK/PB		*/
		0x0207c000,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		fixup_acorn
	}, { "Etoile",		/* Alex de Vries	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "LaCie_NAS",	/* Benjamin Herrenschmidt */
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "CL-PS7500",	/* Philip Blundell	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}, { "Shark",		/* Alexander Schulz	*/
		NO_PARAMS,
		/* do you really mean 0x200000? */
		0x06000000, 0x06000000+0x00200000,
		0, 0, 0, 0, 0,
		NULL
	}, { "SA1100-based",	/* Nicolas Pitre	*/
		NO_PARAMS,
		NO_VIDEO,
		0, 0, 0, 0, 0,
		NULL
	}
};

void __init
setup_arch(char **cmdline_p, unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	struct param_struct *params = NULL;
	struct machine_desc *mdesc;
	char *from = default_command_line;

#if defined(CONFIG_ARCH_ARC)
	__machine_arch_type = MACH_TYPE_ARCHIMEDES;
#elif defined(CONFIG_ARCH_A5K)
	__machine_arch_type = MACH_TYPE_A5K;
#endif

	setup_processor();

	ROOT_DEV = MKDEV(0, 255);

	mdesc = machine_desc + machine_arch_type;
	machine_name = mdesc->name;

	if (mdesc->broken_hlt)
		disable_hlt();

	if (mdesc->soft_reboot)
		reboot_setup("s", NULL);

	if (mdesc->param_offset)
		params = phys_to_virt(mdesc->param_offset);

	if (mdesc->fixup)
		mdesc->fixup(mdesc, params, &from);

	if (params && params->u1.s.page_size != PAGE_SIZE) {
		printk(KERN_WARNING "Warning: bad configuration page, "
		       "trying to continue\n");
		params = NULL;
	}

	if (params) {
		mem_end	  = params->u1.s.page_size *
			    params->u1.s.nr_pages;

		ROOT_DEV	   = to_kdev_t(params->u1.s.rootdev);
		system_rev	   = params->u1.s.system_rev;
		system_serial_low  = params->u1.s.system_serial_low;
		system_serial_high = params->u1.s.system_serial_high;

		setup_ramdisk((params->u1.s.flags & FLAG_RDLOAD) == 0,
			      (params->u1.s.flags & FLAG_RDPROMPT) == 0,
			      params->u1.s.rd_start,
			      params->u1.s.ramdisk_size);

		setup_initrd(params->u1.s.initrd_start,
			     params->u1.s.initrd_size);

		if (!(params->u1.s.flags & FLAG_READONLY))
			root_mountflags &= ~MS_RDONLY;

#ifdef CONFIG_ARCH_RPC
		{
			extern void init_dram_banks(struct param_struct *);
			init_dram_banks(params);
		}
#endif
		mem_end -= vram_size;

		from = params->commandline;
	}

	if (!mem_end)
		mem_end = MEM_SIZE;

	mem_end += PAGE_OFFSET;

	init_task.mm->start_code = (unsigned long) &_text;
	init_task.mm->end_code	 = (unsigned long) &_etext;
	init_task.mm->end_data	 = (unsigned long) &_edata;
	init_task.mm->brk	 = (unsigned long) &_end;

	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';
	parse_cmdline(cmdline_p, from);
	setup_bootmem();

#ifdef CONFIG_NWFPE
	fpe_init();
#endif

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

	*cmdline_p = command_line;
	*memory_start_p = (unsigned long)&_end;
	*memory_end_p = mem_end;
}

int get_cpuinfo(char * buffer)
{
	char *p = buffer;

	p += sprintf(p, "Processor\t: %s %s rev %d (%s)\n",
		     proc_info.manufacturer, proc_info.cpu_name,
		     (int)processor_id & 15, elf_platform);

	p += sprintf(p, "BogoMIPS\t: %lu.%02lu\n",
		     (loops_per_jiffy+2500) / (500000/HZ),
		     ((loops_per_jiffy+2500) / (5000/HZ)) % 100);

	p += sprintf(p, "Hardware\t: %s\n", machine_name);

	p += sprintf(p, "Revision\t: %04x\n",
		     system_rev);

	p += sprintf(p, "Serial\t\t: %08x%08x\n",
		     system_serial_high,
		     system_serial_low);

	return p - buffer;
}
