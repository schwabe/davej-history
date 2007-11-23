/*
 *  linux/arch/m68k/mac/config.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Miscellaneous linux stuff
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
/* keyb */
#include <linux/random.h>
#include <linux/delay.h>
/* keyb */
#include <linux/init.h>

#define BOOTINFO_COMPAT_1_0
#include <asm/setup.h>
#include <asm/bootinfo.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>

#include <asm/macintosh.h>
#include <asm/macints.h>

#include "via6522.h"

/* old bootinfo stuff */

struct mac_booter_data mac_bi_data = {0,};
int mac_bisize = sizeof mac_bi_data;

struct compat_bootinfo compat_boot_info ={0,};
int compat_bisize = sizeof compat_boot_info;

int compat_bi = 0;

/* New bootinfo stuff */

extern int m68k_num_memory;
extern struct mem_info m68k_memory[NUM_MEMINFO];

extern struct mem_info m68k_ramdisk;

extern char m68k_command_line[CL_SIZE];

void *mac_env;		/* Loaded by the boot asm */

extern int mac_keyb_init(void);
extern int mac_kbdrate(struct kbd_repeat *k);
extern void mac_kbd_leds(unsigned int leds);

extern void (*kd_mksound)(unsigned int, unsigned int);
extern void mac_mksound(unsigned int, unsigned int);
extern int mac_floppy_init(void);
extern void mac_floppy_setup(char *,int *);

extern void mac_gettod (int *, int *, int *, int *, int *, int *);

extern void nubus_sweep_video(void);
extern void via_init_clock(void (*func)(int, void *, struct pt_regs *));
extern void mac_debugging_long(int, long);

/* Mac specific debug functions (in debug.c) */
extern void mac_debug_init(void);

#ifdef CONFIG_MAGIC_SYSRQ
static char mac_sysrq_xlate[128] =
	"\000\0331234567890-=\177\t"					/* 0x00 - 0x0f */
	"qwertyuiop[]\r\000as"							/* 0x10 - 0x1f */
	"dfghjkl;'`\000\\zxcv"							/* 0x20 - 0x2f */
	"bnm,./\000\000\000 \000\201\202\203\204\205"	/* 0x30 - 0x3f */
	"\206\207\210\211\212\000\000\000\000\000-\000\000\000+\000"/* 0x40 - 0x4f */
	"\000\000\000\177\000\000\000\000\000\000\000\000\000\000\000\000" /* 0x50 - 0x5f */
	"\000\000\000()/*789456123"						/* 0x60 - 0x6f */
	"0.\r\000\000\000\000\000\000\000\000\000\000\000\000\000";	/* 0x70 - 0x7f */
#endif

extern void (*kd_mksound)(unsigned int, unsigned int);

void mac_get_model(char *str)
{
	strcpy(str,"Macintosh");
}

void mac_bang(int irq, void *vector, struct pt_regs *p)
{
	printk("Resetting ...\n");
	mac_reset();
}

void mac_sched_init(void (*vector)(int, void *, struct pt_regs *))
{
	via_init_clock(vector);
}

unsigned long mac_gettimeoffset (void)
{
	return 0L;
}

extern int console_loglevel;

void mac_gettod (int *yearp, int *monp, int *dayp,
		 int *hourp, int *minp, int *secp)
{
	unsigned long time;
	int leap, oldleap, isleap;
	int mon_days[14] = { -1, 31, 27, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, -1 };

	time = mac_bi_data.boottime - 60*mac_bi_data.gmtbias; /* seconds */

#if 0
	printk("mac_gettod: boottime 0x%lx gmtbias %ld \n",
		mac_bi_data.boottime, mac_bi_data.gmtbias);
#endif	

	*minp = time / 60;
	*secp = time - (*minp * 60);
	time  = *minp;				/* minutes now */

	*hourp = time / 60;
	*minp  = time - (*hourp * 60);
	time   = *hourp;			/* hours now */

	*dayp  = time / 24;
	*hourp = time - (*dayp * 24);
	time   = *dayp;				/* days now ... */

	/* for leap day calculation */
	*yearp = (time / 365) + 1970;		/* approx. year */

	/* leap year calculation - there's an easier way, I bet */
	/* calculate leap days up to previous year */
	oldleap =  (*yearp-1)/4 - (*yearp-1)/100 + (*yearp-1)/400;
	/* calculate leap days incl. this year */
	leap    =  *yearp/4 - *yearp/100 + *yearp/400;
	/* this year a leap year ?? */
	isleap  = (leap != oldleap);

	/* adjust days: days, excluding past leap days since epoch */
	time  -= oldleap - (1970/4 - 1970/100 + 1970/400);

	/* precise year, and day in year */
	*yearp = (time / 365);			/* #years since epoch */
	*dayp  = time - (*yearp * 365) + 1;	/* #days this year (0: Jan 1) */
	*yearp += 70;				/* add epoch :-) */
	time = *dayp;
	
	if (isleap)				/* add leap day ?? */
		mon_days[2] = 28;

	/* count the months */
	for (*monp = 1; time > mon_days[*monp]; (*monp)++) 
		time -= mon_days[*monp];

	*dayp = time;

#if 1
	printk("mac_gettod: %d-%d-%d %d:%d.%d GMT (GMT offset %d)\n",
		*yearp, *monp, *dayp, *hourp, *minp, *secp, 
		(signed long) mac_bi_data.gmtbias);
#endif

	return;
}

void mac_waitbut (void)
{
	;
}

extern struct consw fb_con;
extern struct fb_info *mac_fb_init(long *);
extern void mac_video_setup(char *, int *);

void mac_debug_init (void)
{
	;
}

void (*mac_handlers[8])(int, void *, struct pt_regs *)=
{
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler
};

    /*
     *  Parse a Macintosh-specific record in the bootinfo
     */

__initfunc(int mac_parse_bootinfo(const struct bi_record *record))
{
    int unknown = 0;
    const u_long *data = record->data;

    if (compat_bi)
        return(unknown);

    switch (record->tag) {
	case BI_MAC_MODEL:
	    mac_bi_data.id = *data;
	    break;
	case BI_MAC_VADDR:
	    mac_bi_data.videoaddr = *data;
	    break;
	case BI_MAC_VDEPTH:
	    mac_bi_data.videodepth = *data;
	    break;
	case BI_MAC_VROW:
	    mac_bi_data.videorow = *data;
	    break;
	case BI_MAC_VDIM:
	    mac_bi_data.dimensions = *data;
	    break;
	case BI_MAC_VLOGICAL:
	    mac_bi_data.videological = *data;
	    break;
	case BI_MAC_SCCBASE:
	    mac_bi_data.sccbase = *data;
	    break;
	case BI_MAC_BTIME:
	    mac_bi_data.boottime = *data;
	    break;
	case BI_MAC_GMTBIAS:
	    mac_bi_data.gmtbias = *data;
	    break;
	case BI_MAC_MEMSIZE:
	    mac_bi_data.memsize = *data;
	    break;
	case BI_MAC_CPUID:
	    mac_bi_data.cpuid = *data;
	    break;
	default:
	    unknown = 1;
    }
    return(unknown);
}

__initfunc(void mac_copy_compat(void))
{
    int i;

    compat_bi = 1;
    
    for (i=0; i<compat_boot_info.num_memory; i++) {
    	m68k_memory[m68k_num_memory].addr = compat_boot_info.memory[i].addr;
    	m68k_memory[m68k_num_memory].size = compat_boot_info.memory[i].size;
	m68k_num_memory++;
    }

    m68k_ramdisk.addr = compat_boot_info.ramdisk_addr;
    m68k_ramdisk.size = compat_boot_info.ramdisk_size;

    strncpy(m68k_command_line, (const char *)compat_boot_info.command_line, 
            CL_SIZE);
    m68k_command_line[CL_SIZE-1] = '\0';
                                
    mac_bi_data.id = compat_boot_info.bi_mac.id;
    mac_bi_data.videoaddr =  compat_boot_info.bi_mac.videoaddr;
    mac_bi_data.videodepth = compat_boot_info.bi_mac.videodepth;
    mac_bi_data.videorow = compat_boot_info.bi_mac.videorow;
    mac_bi_data.dimensions = compat_boot_info.bi_mac.dimensions;
    mac_bi_data.videological = compat_boot_info.bi_mac.videological;
    mac_bi_data.sccbase = compat_boot_info.bi_mac.sccbase;
    mac_bi_data.boottime = compat_boot_info.bi_mac.boottime;
    mac_bi_data.gmtbias = compat_boot_info.bi_mac.gmtbias;
    mac_bi_data.memsize = compat_boot_info.bi_mac.memsize;
    mac_bi_data.cpuid = compat_boot_info.bi_mac.cpuid;

}

__initfunc(void config_mac(void))
{

    if (MACH_IS_ATARI || MACH_IS_AMIGA) {
      printk("ERROR: no Mac, but config_mac() called!! \n");
    }
    
    mac_debugging_penguin(5);

    mac_debug_init();
        
    mach_sched_init      = mac_sched_init;
    mach_keyb_init       = mac_keyb_init;
    mach_kbdrate         = mac_kbdrate;
    mach_kbd_leds        = mac_kbd_leds;
    mach_init_IRQ        = mac_init_IRQ;
    mach_request_irq     = mac_request_irq;
    mach_free_irq        = mac_free_irq;
    enable_irq           = mac_enable_irq;
    disable_irq          = mac_disable_irq;
#if 1
    mach_default_handler = &mac_handlers;
#endif
    mach_get_model	 = mac_get_model;
    mach_get_irq_list	 = mac_get_irq_list;
    mach_gettimeoffset   = mac_gettimeoffset;
    mach_gettod          = mac_gettod;
#if 0
    mach_mksound         = mac_mksound;
#endif
    mach_reset           = mac_reset;
#ifdef CONFIG_BLK_DEV_FD
    mach_floppy_init	 = mac_floppy_init;
    mach_floppy_setup	 = mac_floppy_setup;
#endif
    conswitchp	         = &fb_con;
    mach_max_dma_address = 0xffffffff;
#if 0
    mach_debug_init	 = mac_debug_init;
    mach_video_setup	 = mac_video_setup;
#endif
    kd_mksound		 = mac_mksound;
#ifdef CONFIG_MAGIC_SYSRQ
    mach_sysrq_key = 98;          /* HELP */
    mach_sysrq_shift_state = 8;   /* Alt */
    mach_sysrq_shift_mask = 0xff; /* all modifiers except CapsLock */
    mach_sysrq_xlate = mac_sysrq_xlate;
#endif
#ifdef CONFIG_HEARTBEAT
#if 0
    mach_heartbeat = mac_heartbeat;
    mach_heartbeat_irq = IRQ_MAC_TIMER;
#endif
#endif

    /*
     * Determine hardware present
     */
     
    mac_identify();
    mac_report_hardware();

    /* goes on forever if timers broken */
#ifdef MAC_DEBUG_SOUND
    mac_mksound(1000,10);
#endif

    /*
     * Check for machine specific fixups.
     */

    nubus_sweep_video();
}	


/*
 *	Macintosh Table
 */
 
struct mac_model *macintosh_config;

static struct mac_model mac_data_table[]=
{
	/*
	 *	Original MacII hardware
	 *	
	 */
	 
	{	MAC_MODEL_II,	"II",		MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_NUBUS},
	{	MAC_MODEL_IIX,	"IIx",		MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_NUBUS},
	{	MAC_MODEL_IICX,	"IIcx",		MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_NUBUS},
	{	MAC_MODEL_SE30, "SE/30",	MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_NUBUS},
	
	/*
	 *	Weirdified MacII hardware - all subtley different. Gee thanks
	 *	Apple. All these boxes seem to have VIA2 in a different place to
	 *	the MacII (+1A000 rather than +4000)
	 *
	 *	The IIfx apparently has different ADB hardware, and stuff
	 *	so zany nobody knows how to drive it.
	 */

	{	MAC_MODEL_IICI,	"IIci",	MAC_ADB_II,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIFX,	"IIfx",	MAC_ADB_NONE,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IISI, "IIsi",	MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIVI,	"IIvi",	MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIVX,	"IIvx",	MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	
	/*
	 *	Classic models (guessing: similar to SE/30 ?? Nope, similar to LC ...)
	 */

	{	MAC_MODEL_CLII, "Classic II",		MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,     MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_CCL,  "Color Classic",	MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,     MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Some Mac LC machines. Basically the same as the IIci
	 */
	
	{	MAC_MODEL_LCII,	"LC II",  MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_LCIII,"LC III", MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Quadra (only 68030 ones will actually work!). Not much odd. Video is at
	 *	0xF9000000, via is like a MacII. We label it differently as some of the
	 *	stuff connected to VIA2 seems different. Better SCSI chip and ???? onboard ethernet
	 *	in all cases using a NatSemi SONIC. The 700, 900 and 950 have some I/O chips in the wrong
	 *	place to confuse us. The 840 seems to have a scsi location of its own
	 */	 
	 
	{	MAC_MODEL_Q605, "Quadra 605", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q610, "Quadra 610", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q630, "Quadra 630", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
 	{	MAC_MODEL_Q650, "Quadra 650", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	/*	The Q700 does have a NS Sonic */
#if 0
	{	MAC_MODEL_Q700, "Quadra 700", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE, MAC_SCC_QUADRA2,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q800, "Quadra 800", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
#else
	{	MAC_MODEL_Q700, "Quadra 700", MAC_ADB_II, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE, MAC_SCC_QUADRA2,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q800, "Quadra 800", MAC_ADB_II, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
#endif
	/* Does the 840 have ethernet ??? documents seem to indicate its not quite a
	   Quadra in this respect ? */
	{	MAC_MODEL_Q840, "Quadra 840", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA3, MAC_IDE_NONE, MAC_SCC_II,	        MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_Q900, "Quadra 900", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE, MAC_SCC_QUADRA2,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q950, "Quadra 950", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE, MAC_SCC_QUADRA2,	MAC_ETHER_SONIC,	MAC_NUBUS},

	/* 
	 *	Performa - more LC type machines
	 */

	{	MAC_MODEL_P460, "Performa 460", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P475, "Performa 475", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE, MAC_NUBUS},
	{	MAC_MODEL_P520, "Performa 520", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P550, "Performa 550", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P575, "Performa 575", MAC_ADB_CUDA, MAC_VIA_IIci,   MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_TV,   "TV",           MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
#if 0	/* other sources seem to suggest the P630/Q630/LC630 is more like LCIII */
	{	MAC_MODEL_P630, "Performa 630", MAC_ADB_IISI, MAC_VIA_IIci,   MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
#endif
	/*
	 *	Centris - just guessing again; maybe like Quadra
	 */

	{	MAC_MODEL_C610, "Centris 610",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_C650, "Centris 650",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_C660, "Centris 660AV", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *      Power books - seem similar to early Quadras ? (most have 030 though)
	 */

	{	MAC_MODEL_PB140,  "PowerBook 140",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB145,  "PowerBook 145",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	/*	The PB150 has IDE, and IIci style VIA */
	{	MAC_MODEL_PB150,  "PowerBook 150",   MAC_ADB_CUDA, MAC_VIA_IIci,   MAC_SCSI_QUADRA, MAC_IDE_PB,	  MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB160,  "PowerBook 160",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB165,  "PowerBook 165",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB165C, "PowerBook 165c",  MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB170,  "PowerBook 170",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB180,  "PowerBook 180",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB180C, "PowerBook 180c",  MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB190,  "PowerBook 190cs", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB520,  "PowerBook 520",   MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *      Power book Duos - similar to Power books, I hope
	 */

	{	MAC_MODEL_PB210,  "PowerBook Duo 210",  MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB230,  "PowerBook Duo 230",  MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB250,  "PowerBook Duo 250",  MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB270C, "PowerBook Duo 270c", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB280,  "PowerBook Duo 280",  MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB280C, "PowerBook Duo 280c", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Other stuff ??
	 */
	{	-1, NULL, 0,0,0,}
};

void mac_identify(void)
{
	struct mac_model *m=&mac_data_table[0];

	/* Penguin data useful? */	
	int model = mac_bi_data.id;
	if (!model) {
		/* no bootinfo model id -> NetBSD booter was used! */
		/* XXX FIXME: breaks for model > 31 */
		model=(mac_bi_data.cpuid>>2)&63;
		printk ("No bootinfo model ID, using cpuid instead (hey, use Penguin!)\n");
	}

	printk ("Detected Macintosh model: %d \n", model);
	
	while(m->ident != -1)
	{
		if(m->ident == model)
			break;
		m++;
	}
	if(m->ident==-1)
	{
		printk("\nUnknown macintosh model %d, probably unsupported.\n", 
			model);
		mac_debugging_long(1, (long) 0x55555555);
		mac_debugging_long(1, (long) model);
		model = MAC_MODEL_Q800;
		printk("Defaulting to: Quadra800, model id %d\n", model);
		printk("Please report this case to linux-mac68k@wave.lm.com\n");
		m=&mac_data_table[0];
		while(m->ident != -1)
		{
			if(m->ident == model)
				break;
			m++;
		}
		if(m->ident==-1)
			mac_boom(5);
	}

	/*
	 * Report booter data:
	 */
	printk (" Penguin (bootinfo version %d) data:\n", 2-compat_bi);
	printk (" Video: addr 0x%lx row 0x%lx depth %lx dimensions %d x %d\n", 
		mac_bi_data.videoaddr, mac_bi_data.videorow, 
		mac_bi_data.videodepth, mac_bi_data.dimensions & 0xFFFF, 
		mac_bi_data.dimensions >> 16); 
	printk (" Boottime: 0x%lx GMTBias: 0x%lx \n",
		mac_bi_data.boottime, mac_bi_data.gmtbias); 
	printk (" Videological 0x%lx, SCC at 0x%lx \n",
		mac_bi_data.videological, mac_bi_data.sccbase); 
	printk (" Machine ID: %ld CPUid: 0x%lx memory size: 0x%lx \n",
		mac_bi_data.id, mac_bi_data.cpuid, mac_bi_data.memsize); 
	printk ("Ramdisk: addr 0x%lx size 0x%lx\n", 
		m68k_ramdisk.addr, m68k_ramdisk.size);

	/*
	 *	Save the pointer
	 */

	macintosh_config=m;

	/*
	 * TODO: set the various fields in macintosh_config->hw_present here!
	 */

}

void mac_report_hardware(void)
{
	printk("Apple Macintosh %s\n", macintosh_config->name);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */