/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org> 
 */

#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/blk.h>
#include <linux/hdreg.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/bugs.h>

#ifdef CONFIG_ARCH_S390
#include <asm/s390mach.h>
#endif

#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

#ifdef CONFIG_DIO
#include <linux/dio.h>
#endif

#ifdef CONFIG_ZORRO
#include <linux/zorro.h>
#endif

#ifdef CONFIG_MTRR
#  include <asm/mtrr.h>
#endif

#ifdef CONFIG_DASD
#include <asm/dasd.h>
#endif

#ifdef CONFIG_BLK_DEV_XPRAM
#include "../drivers/s390/block/xpram.h"
#endif

#ifdef CONFIG_MAC
extern void nubus_init(void);
#endif

extern int irda_device_init(void);

/*
 * Versions of gcc older than that listed below may actually compile
 * and link okay, but the end product can have subtle run time bugs.
 * To avoid associated bogus bug reports, we flatly refuse to compile
 * with a gcc that is known to be too old from the very beginning.
 */
#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 6)
#error sorry, your GCC is too old. It builds incorrect kernels.
#endif

extern char _stext, _etext;
extern char *linux_banner;

extern int console_loglevel;

static int init(void *);
extern int bdflush(void *);
extern int kupdate(void *);
extern int kswapd(void *);
extern void kswapd_setup(void);
extern unsigned long init_IRQ( unsigned long);
extern void init_modules(void);
extern long console_init(long, long);
extern void sock_init(void);
extern void uidcache_init(void);
extern void mca_init(void);
extern void sbus_init(void);
extern void ppc_init(void);
extern void sysctl_init(void);
extern void filescache_init(void);
extern void signals_init(void);

extern void device_setup(void);
extern void binfmt_setup(void);
extern void free_initmem(void);
extern void filesystem_setup(void);

#ifdef CONFIG_ARCH_ACORN
extern void ecard_init(void);
#endif

extern void smp_setup(char *str, int *ints);
#ifdef __i386__
extern void ioapic_pirq_setup(char *str, int *ints);
extern void ioapic_setup(char *str, int *ints);
#endif
#ifdef CONFIG_MICROCODE
extern int microcode_init(void);
#endif
extern void no_scroll(char *str, int *ints);
extern void kbd_reset_setup(char *str, int *ints);
extern void aux_reconnect_setup(char *str, int *ints);
extern void panic_setup(char *str, int *ints);
extern void bmouse_setup(char *str, int *ints);
extern void msmouse_setup(char *str, int *ints);
extern void console_setup(char *str, int *ints);
#ifdef CONFIG_PRINTER
extern void lp_setup(char *str, int *ints);
#endif
#ifdef CONFIG_VIDEO_CPIA_PP
extern void cpia_pp_setup(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_AMIGA
extern void js_am_setup(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_ANALOG
extern void js_an_setup(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_ASSASSIN
extern void js_as_setup(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_CONSOLE
extern void js_console_setup(char *str, int *ints);
extern void js_console_setup_2(char *str, int *ints);
extern void js_console_setup_3(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_DB9
extern void js_db9_setup(char *str, int *ints);
extern void js_db9_setup_2(char *str, int *ints);
extern void js_db9_setup_3(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_TURBOGRAFX
extern void js_tg_setup(char *str, int *ints);
extern void js_tg_setup_2(char *str, int *ints);
extern void js_tg_setup_3(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_LIGHTNING
extern void js_l4_setup(char *str, int *ints);
#endif
#ifdef CONFIG_JOY_PCI
extern void js_pci_setup(char *str, int *ints);
#endif
extern void eth_setup(char *str, int *ints);
#ifdef CONFIG_ARCNET_COM20020
extern void com20020_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ARCNET_RIM_I
extern void arcrimi_setup(char *str, int *ints);
#endif
#ifdef CONFIG_CTC  
extern void ctc_setup(char *str, int *ints);
#endif
#ifdef CONFIG_IUCV
extern void iucv_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ARCNET_COM90xxIO
extern void com90io_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ARCNET_COM90xx
extern void com90xx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DECNET
extern void decnet_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BLK_DEV_XD
extern void xd_setup(char *str, int *ints);
extern void xd_manual_geo_init(char *str, int *ints);
#endif
#ifdef CONFIG_BLK_DEV_IDE
extern void ide_setup(char *);
#endif
#ifdef CONFIG_PARIDE_PD
extern void pd_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARIDE_PF
extern void pf_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARIDE_PT
extern void pt_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARIDE_PG
extern void pg_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARIDE_PCD
extern void pcd_setup(char *str, int *ints);
#endif
#ifdef CONFIG_3215
extern void con3215_setup(char *str, int *ints);
#endif
#ifdef CONFIG_MDISK
extern void mdisk_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BLK_DEV_XPRAM
extern void xpram_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ARCH_S390
extern void vmhalt_setup(char *str, int *ints);
extern void vmpoff_setup(char *str, int *ints);
#endif
extern void floppy_setup(char *str, int *ints);
extern void st_setup(char *str, int *ints);
extern void osst_setup(char *str, int *ints);
extern void st0x_setup(char *str, int *ints);
extern void advansys_setup(char *str, int *ints);
extern void tmc8xx_setup(char *str, int *ints);
extern void t128_setup(char *str, int *ints);
extern void pas16_setup(char *str, int *ints);
extern void generic_NCR5380_setup(char *str, int *intr);
extern void generic_NCR53C400_setup(char *str, int *intr);
extern void generic_NCR53C400A_setup(char *str, int *intr);
extern void generic_DTC3181E_setup(char *str, int *intr);
extern void aha152x_setup(char *str, int *ints);
extern void aha1542_setup(char *str, int *ints);
extern void gdth_setup(char *str, int *ints);
extern void aic7xxx_setup(char *str, int *ints);
extern void ips_setup(char *str, int *ints);
extern void AM53C974_setup(char *str, int *ints);
extern void BusLogic_Setup(char *str, int *ints);
extern void ncr53c8xx_setup(char *str, int *ints);
extern void sym53c8xx_setup(char *str, int *ints);
extern void eata2x_setup(char *str, int *ints);
extern void u14_34f_setup(char *str, int *ints);
extern void fdomain_setup(char *str, int *ints);
extern void ibmmca_scsi_setup(char *str, int *ints);
extern void fd_mcs_setup(char *str, int *ints);
extern void in2000_setup(char *str, int *ints);
extern void NCR53c406a_setup(char *str, int *ints);
extern void sym53c416_setup(char *str, int *ints);
extern void wd7000_setup(char *str, int *ints);
extern void dc390_setup(char* str, int *ints);
extern void scsi_luns_setup(char *str, int *ints);
extern void scsi_logging_setup(char *str, int *ints);
extern void sound_setup(char *str, int *ints);
extern void reboot_setup(char *str, int *ints);
extern void video_setup(char *str, int *ints);
#ifdef CONFIG_CDU31A
extern void cdu31a_setup(char *str, int *ints);
#endif CONFIG_CDU31A
#ifdef CONFIG_BLK_DEV_PS2
extern void ed_setup(char *str, int *ints);
extern void tp720_setup(char *str, int *ints);
#endif CONFIG_BLK_DEV_PS2
#ifdef CONFIG_MCD
extern void mcd_setup(char *str, int *ints);
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
extern void mcdx_setup(char *str, int *ints);
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
extern void sbpcd_setup(char *str, int *ints);
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
extern void aztcd_setup(char *str, int *ints);
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
extern void sonycd535_setup(char *str, int *ints);
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
extern void gscd_setup(char *str, int *ints);
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
extern void cm206_setup(char *str, int *ints);
#endif CONFIG_CM206
#ifdef CONFIG_OPTCD
extern void optcd_setup(char *str, int *ints);
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
extern void sjcd_setup(char *str, int *ints);
#endif CONFIG_SJCD
#ifdef CONFIG_ISP16_CDI
extern void isp16_setup(char *str, int *ints);
#endif CONFIG_ISP16_CDI
#ifdef CONFIG_BLK_DEV_RAM
static void ramdisk_start_setup(char *str, int *ints);
static void load_ramdisk(char *str, int *ints);
static void prompt_ramdisk(char *str, int *ints);
static void ramdisk_size(char *str, int *ints);
#ifdef CONFIG_BLK_DEV_INITRD
static void no_initrd(char *s,int *ints);
#endif
#endif CONFIG_BLK_DEV_RAM
#ifdef CONFIG_ISDN_DRV_ICN
extern void icn_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ISDN_DRV_HISAX
extern void HiSax_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DIGIEPCA
extern void epca_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ISDN_DRV_PCBIT
extern void pcbit_setup(char *str, int *ints);
#endif

#ifdef CONFIG_ATARIMOUSE
extern void atari_mouse_setup (char *str, int *ints);
#endif
#ifdef CONFIG_DMASOUND
extern void dmasound_setup (char *str, int *ints);
#endif
#ifdef CONFIG_ATARI_SCSI
extern void atari_scsi_setup (char *str, int *ints);
#endif
extern void stram_swap_setup (char *str, int *ints);
extern void wd33c93_setup (char *str, int *ints);
extern void gvp11_setup (char *str, int *ints);
extern void ncr53c7xx_setup (char *str, int *ints);
extern void sim710_setup (char *str, int *ints);
#ifdef CONFIG_MAC_SCSI
extern void mac_scsi_setup (char *str, int *ints);
#endif

#ifdef CONFIG_CYCLADES
extern void cy_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DIGI
extern void pcxx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_RISCOM8
extern void riscom8_setup(char *str, int *ints);
#endif

extern void n2_setup(char *str, int *ints);
extern void c101_setup(char *str, int *ints);

#ifdef CONFIG_SPECIALIX
extern void specialix_setup(char *str, int *ints);
#endif
#ifdef CONFIG_DMASCC
extern void dmascc_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BAYCOM_PAR
extern void baycom_par_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BAYCOM_SER_FDX
extern void baycom_ser_fdx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BAYCOM_SER_HDX
extern void baycom_ser_hdx_setup(char *str, int *ints);
#endif
#ifdef CONFIG_SOUNDMODEM
extern void sm_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ADBMOUSE
extern void adb_mouse_setup(char *str, int *ints);
#endif
#ifdef CONFIG_COMPUTONE
extern void ip2_setup(char *str, int *ints);
#endif
#ifdef CONFIG_WDT
extern void wdt_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PARPORT
extern void parport_setup(char *str, int *ints);
#endif
#ifdef CONFIG_PLIP
extern void plip_setup(char *str, int *ints);
#endif
#ifdef CONFIG_HFMODEM
extern void hfmodem_setup(char *str, int *ints);
#endif
#ifdef CONFIG_IP_PNP
extern void ip_auto_config_setup(char *str, int *ints);
#endif
#ifdef CONFIG_ROOT_NFS
extern void nfs_root_setup(char *str, int *ints);
#endif
#ifdef CONFIG_FTAPE
extern void ftape_setup(char *str, int *ints);
#endif
#ifdef CONFIG_3215_CONSOLE
extern int con3215_activate(void);
#endif
#ifdef CONFIG_MDA_CONSOLE
extern void mdacon_setup(char *str, int *ints);
#endif
#ifdef CONFIG_LTPC
extern void ltpc_setup(char *str, int *ints);
#endif
#ifdef CONFIG_BLK_CPQ_DA
extern void cpqarray_setup(char *str, int *ints);
#endif

#if defined(CONFIG_SYSVIPC)
extern void ipc_init(void);
#endif
#if defined(CONFIG_QUOTA)
extern void dquot_init_hash(void);
#endif

#ifdef CONFIG_MD_BOOT
extern void md_setup(char *str,int *ints) __init;
#endif

#ifdef CONFIG_AGP
extern int agp_init (void);
#endif

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS 8
#define MAX_INIT_ENVS 8

extern void time_init(void);

static unsigned long memory_start = 0;
static unsigned long memory_end = 0;

int rows, cols;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load 2 = dual disk */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_size;		/* Size of the ramdisk(s) */
extern int rd_image_start;	/* starting block # of image */
#ifdef CONFIG_BLK_DEV_INITRD
kdev_t real_root_dev;
#endif
#endif

int root_mountflags = MS_RDONLY;
char *execute_command = NULL;

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
static char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };

char *get_options(char *str, int *ints)
{
	char *cur = str;
	int i=1;

	while (cur && (*cur=='-' || isdigit(*cur)) && i <= 10) {
		ints[i++] = simple_strtol(cur,NULL,0);
		if ((cur = strchr(cur,',')) != NULL)
			cur++;
	}
	ints[0] = i-1;
	return(cur);
}

static void __init profile_setup(char *str, int *ints)
{
	if (ints[0] > 0)
		prof_shift = (unsigned long) ints[1];
	else
		prof_shift = 2;
}


static struct dev_name_struct {
	const char *name;
	const int num;
} root_dev_names[] __initdata = {
	{ "nfs",     0x00ff },
	{ "loop",    0x0700 },
	{ "hda",     0x0300 },
	{ "hdb",     0x0340 },
	{ "hdc",     0x1600 },
	{ "hdd",     0x1640 },
	{ "hde",     0x2100 },
	{ "hdf",     0x2140 },
	{ "hdg",     0x2200 },
	{ "hdh",     0x2240 },
	{ "hdi",     0x3800 },
	{ "hdj",     0x3840 },
	{ "hdk",     0x3900 },
	{ "hdl",     0x3940 },
	{ "sda",     0x0800 },
	{ "sdb",     0x0810 },
	{ "sdc",     0x0820 },
	{ "sdd",     0x0830 },
	{ "sde",     0x0840 },
	{ "sdf",     0x0850 },
	{ "sdg",     0x0860 },
	{ "sdh",     0x0870 },
	{ "sdi",     0x0880 },
	{ "sdj",     0x0890 },
	{ "sdk",     0x08a0 },
	{ "sdl",     0x08b0 },
	{ "sdm",     0x08c0 },
	{ "sdn",     0x08d0 },
	{ "sdo",     0x08e0 },
	{ "sdp",     0x08f0 },
	{ "rd/c0d0p",0x3000 },
	{ "rd/c0d1p",0x3008 },
	{ "rd/c0d2p",0x3010 },
	{ "rd/c0d3p",0x3018 },
	{ "rd/c0d4p",0x3020 },
	{ "rd/c0d5p",0x3028 },
	{ "rd/c0d6p",0x3030 },
	{ "rd/c0d7p",0x3038 },
	{ "rd/c0d8p",0x3040 },
	{ "rd/c0d9p",0x3048 },
	{ "rd/c0d10p",0x3050 },
	{ "rd/c0d11p",0x3058 },
	{ "rd/c0d12p",0x3060 },
	{ "rd/c0d13p",0x3068 },
	{ "rd/c0d14p",0x3070 },
	{ "rd/c0d15p",0x3078 },
	{ "ida/c0d0p",0x4800 },
	{ "ida/c0d1p",0x4810 },
	{ "ida/c0d2p",0x4820 },
	{ "ida/c0d3p",0x4830 },
	{ "ida/c0d4p",0x4840 },
	{ "ida/c0d5p",0x4850 },
	{ "ida/c0d6p",0x4860 },
	{ "ida/c0d7p",0x4870 },
	{ "ida/c0d8p",0x4880 },
	{ "ida/c0d9p",0x4890 },
	{ "ida/c0d10p",0x48A0 },
	{ "ida/c0d11p",0x48B0 },
	{ "ida/c0d12p",0x48C0 },
	{ "ida/c0d13p",0x48D0 },
	{ "ida/c0d14p",0x48E0 },
	{ "ida/c0d15p",0x48F0 },
	{ "ada",     0x1c00 },
	{ "adb",     0x1c10 },
	{ "adc",     0x1c20 },
	{ "add",     0x1c30 },
	{ "ade",     0x1c40 },
	{ "fd",      0x0200 },
	{ "md",      0x0900 },	     
	{ "xda",     0x0d00 },
	{ "xdb",     0x0d40 },
	{ "ram",     0x0100 },
	{ "scd",     0x0b00 },
	{ "mcd",     0x1700 },
	{ "cdu535",  0x1800 },
	{ "sonycd",  0x1800 },
	{ "aztcd",   0x1d00 },
	{ "cm206cd", 0x2000 },
	{ "gscd",    0x1000 },
	{ "sbpcd",   0x1900 },
	{ "eda",     0x2400 },
	{ "edb",     0x2440 },
	{ "pda",	0x2d00 },
	{ "pdb",	0x2d10 },
	{ "pdc",	0x2d20 },
	{ "pdd",	0x2d30 },
	{ "pcd",	0x2e00 },
	{ "pf",		0x2f00 },
	{ "apblock", APBLOCK_MAJOR << 8},
	{ "ddv", DDV_MAJOR << 8},
#ifdef CONFIG_MDISK
        { "mnda", (MDISK_MAJOR << MINORBITS)},
        { "mndb", (MDISK_MAJOR << MINORBITS) + 1},
        { "mndc", (MDISK_MAJOR << MINORBITS) + 2},
        { "mndd", (MDISK_MAJOR << MINORBITS) + 3},
        { "mnde", (MDISK_MAJOR << MINORBITS) + 4},
        { "mndf", (MDISK_MAJOR << MINORBITS) + 5},
        { "mndg", (MDISK_MAJOR << MINORBITS) + 6},
        { "mndh", (MDISK_MAJOR << MINORBITS) + 7},
#endif
#ifdef CONFIG_DASD
       { "dasda", (DASD_MAJOR << MINORBITS) },
       { "dasdb", (DASD_MAJOR << MINORBITS) + (1 << 2) },
       { "dasdc", (DASD_MAJOR << MINORBITS) + (2 << 2) },
       { "dasdd", (DASD_MAJOR << MINORBITS) + (3 << 2) },
       { "dasde", (DASD_MAJOR << MINORBITS) + (4 << 2) },
       { "dasdf", (DASD_MAJOR << MINORBITS) + (5 << 2) },
       { "dasdg", (DASD_MAJOR << MINORBITS) + (6 << 2) },
       { "dasdh", (DASD_MAJOR << MINORBITS) + (7 << 2) },
       { "dasdi", (DASD_MAJOR << MINORBITS) + (8 << 2) },
       { "dasdj", (DASD_MAJOR << MINORBITS) + (9 << 2) },
       { "dasdk", (DASD_MAJOR << MINORBITS) + (10 << 2) },
       { "dasdl", (DASD_MAJOR << MINORBITS) + (11 << 2) },
       { "dasdm", (DASD_MAJOR << MINORBITS) + (12 << 2) },
       { "dasdn", (DASD_MAJOR << MINORBITS) + (13 << 2) },
       { "dasdo", (DASD_MAJOR << MINORBITS) + (14 << 2) },
       { "dasdp", (DASD_MAJOR << MINORBITS) + (15 << 2) },
       { "dasdq", (DASD_MAJOR << MINORBITS) + (16 << 2) },
       { "dasdr", (DASD_MAJOR << MINORBITS) + (17 << 2) },
       { "dasds", (DASD_MAJOR << MINORBITS) + (18 << 2) },
       { "dasdt", (DASD_MAJOR << MINORBITS) + (19 << 2) },
       { "dasdu", (DASD_MAJOR << MINORBITS) + (20 << 2) },
       { "dasdv", (DASD_MAJOR << MINORBITS) + (21 << 2) },
       { "dasdw", (DASD_MAJOR << MINORBITS) + (22 << 2) },
       { "dasdx", (DASD_MAJOR << MINORBITS) + (23 << 2) },
       { "dasdy", (DASD_MAJOR << MINORBITS) + (24 << 2) },
       { "dasdz", (DASD_MAJOR << MINORBITS) + (25 << 2) },
#endif
#ifdef CONFIG_BLK_DEV_XPRAM
       { "xpram0", (XPRAM_MAJOR << MINORBITS) },
       { "xpram1", (XPRAM_MAJOR << MINORBITS) + 1 },
       { "xpram2", (XPRAM_MAJOR << MINORBITS) + 2 },
       { "xpram3", (XPRAM_MAJOR << MINORBITS) + 3 },
       { "xpram4", (XPRAM_MAJOR << MINORBITS) + 4 },
       { "xpram5", (XPRAM_MAJOR << MINORBITS) + 5 },
       { "xpram6", (XPRAM_MAJOR << MINORBITS) + 6 },
       { "xpram7", (XPRAM_MAJOR << MINORBITS) + 7 },
       { "xpram8", (XPRAM_MAJOR << MINORBITS) + 8 },
       { "xpram9", (XPRAM_MAJOR << MINORBITS) + 9 },
       { "xpram10", (XPRAM_MAJOR << MINORBITS) + 10 },
       { "xpram11", (XPRAM_MAJOR << MINORBITS) + 11 },
       { "xpram12", (XPRAM_MAJOR << MINORBITS) + 12 },
       { "xpram13", (XPRAM_MAJOR << MINORBITS) + 13 },
       { "xpram14", (XPRAM_MAJOR << MINORBITS) + 14 },
       { "xpram15", (XPRAM_MAJOR << MINORBITS) + 15 },
       { "xpram16", (XPRAM_MAJOR << MINORBITS) + 16 },
       { "xpram17", (XPRAM_MAJOR << MINORBITS) + 17 },
       { "xpram18", (XPRAM_MAJOR << MINORBITS) + 18 },
       { "xpram19", (XPRAM_MAJOR << MINORBITS) + 19 },
       { "xpram20", (XPRAM_MAJOR << MINORBITS) + 20 },
       { "xpram21", (XPRAM_MAJOR << MINORBITS) + 21 },
       { "xpram22", (XPRAM_MAJOR << MINORBITS) + 22 },
       { "xpram23", (XPRAM_MAJOR << MINORBITS) + 23 },
       { "xpram24", (XPRAM_MAJOR << MINORBITS) + 24 },
       { "xpram25", (XPRAM_MAJOR << MINORBITS) + 25 },
       { "xpram26", (XPRAM_MAJOR << MINORBITS) + 26 },
       { "xpram27", (XPRAM_MAJOR << MINORBITS) + 27 },
       { "xpram28", (XPRAM_MAJOR << MINORBITS) + 28 },
       { "xpram29", (XPRAM_MAJOR << MINORBITS) + 29 },
       { "xpram30", (XPRAM_MAJOR << MINORBITS) + 30 },
       { "xpram31", (XPRAM_MAJOR << MINORBITS) + 31 },
#endif
	{ NULL, 0 }
};

kdev_t __init name_to_kdev_t(char *line)
{
	int base = 0;
	if (strncmp(line,"/dev/",5) == 0) {
		struct dev_name_struct *dev = root_dev_names;
		line += 5;
		do {
			int len = strlen(dev->name);
			if (strncmp(line,dev->name,len) == 0) {
				line += len;
				base = dev->num;
				break;
			}
			dev++;
		} while (dev->name);
	}
	return to_kdev_t(base + simple_strtoul(line,NULL,base?10:16));
}

static void __init root_dev_setup(char *line, int *num)
{
	ROOT_DEV = name_to_kdev_t(line);
}

/*
 * List of kernel command line parameters. The first table lists parameters
 * which are subject to values parsing (leading numbers are converted to
 * an array of ints and chopped off the string), the second table contains
 * the few exceptions which obey their own syntax rules.
 */

struct kernel_param {
	const char *str;
	void (*setup_func)(char *, int *);
};

static struct kernel_param cooked_params[] __initdata = {
/* FIXME: make PNP just become reserve_setup */
#ifndef CONFIG_KERNEL_PNP_RESOURCE
	{ "reserve=", reserve_setup },
#else
	{ "reserve=", pnp_reserve_setup },
#endif
	{ "profile=", profile_setup },
#ifdef __SMP__
	{ "nosmp", smp_setup },
	{ "maxcpus=", smp_setup },
#ifdef CONFIG_X86_IO_APIC
	{ "noapic", ioapic_setup },
	{ "pirq=", ioapic_pirq_setup },
#endif

#endif
#ifdef CONFIG_BLK_DEV_RAM
	{ "ramdisk_start=", ramdisk_start_setup },
	{ "load_ramdisk=", load_ramdisk },
	{ "prompt_ramdisk=", prompt_ramdisk },
	{ "ramdisk=", ramdisk_size },
	{ "ramdisk_size=", ramdisk_size },
#ifdef CONFIG_BLK_DEV_INITRD
	{ "noinitrd", no_initrd },
#endif
#endif

#ifdef CONFIG_CTC
        { "ctc=", ctc_setup } ,
#endif
#ifdef CONFIG_IUCV
        { "iucv=", iucv_setup } ,
#endif

#ifdef CONFIG_FB
	{ "video=", video_setup },
#endif
	{ "panic=", panic_setup },
	{ "console=", console_setup },
#ifdef CONFIG_VGA_CONSOLE
	{ "no-scroll", no_scroll },
#endif
#ifdef CONFIG_MDA_CONSOLE
	{ "mdacon=", mdacon_setup },
#endif
#ifdef CONFIG_VT
	{ "kbd-reset", kbd_reset_setup },
#endif
#if defined(CONFIG_PSMOUSE) && defined (CONFIG_VT)
	{ "psaux-reconnect", aux_reconnect_setup },
#endif
#ifdef CONFIG_BUGi386
	{ "no-hlt", no_halt },
	{ "no387", no_387 },
	{ "reboot=", reboot_setup },
#endif
#ifdef CONFIG_MCA
	{ "mca-pentium", mca_pentium },
#endif
#ifdef CONFIG_INET
	{ "ether=", eth_setup },
#endif
#ifdef CONFIG_ARCNET_COM20020
	{ "com20020=", com20020_setup },
#endif
#ifdef CONFIG_ARCNET_RIM_I
	{ "arcrimi=", arcrimi_setup },
#endif
#ifdef CONFIG_ARCNET_COM90xxIO
	{ "com90io=", com90io_setup },
#endif
#ifdef CONFIG_ARCNET_COM90xx
	{ "com90xx=", com90xx_setup },
#endif
#ifdef CONFIG_DECNET
	{ "decnet=", decnet_setup },
#endif
#ifdef CONFIG_PRINTER
        { "lp=", lp_setup },
#endif
#ifdef CONFIG_VIDEO_CPIA_PP
        { "cpia_pp=", cpia_pp_setup },
#endif
#ifdef CONFIG_JOY_AMIGA
	{ "js_am=", js_am_setup },
#endif
#ifdef CONFIG_JOY_ANALOG
	{ "js_an=", js_an_setup },
#endif
#ifdef CONFIG_JOY_ASSASSIN
	{ "js_as=", js_as_setup },
#endif
#ifdef CONFIG_JOY_CONSOLE
	{ "js_console=", js_console_setup },
	{ "js_console2=", js_console_setup_2 },
	{ "js_console3=", js_console_setup_3 },
#endif
#ifdef CONFIG_JOY_DB9
	{ "js_db9=", js_db9_setup },
	{ "js_db9_2=", js_db9_setup_2 },
	{ "js_db9_3=", js_db9_setup_3 },
#endif
#ifdef CONFIG_JOY_TURBOGRAFX
	{ "js_tg=", js_tg_setup },
	{ "js_tg_2=", js_tg_setup_2 },
	{ "js_tg_3=", js_tg_setup_3 },
#endif
#ifdef CONFIG_JOY_LIGHTNING
	{ "js_l4=", js_l4_setup },
#endif
#ifdef CONFIG_JOY_PCI
	{ "js_pci=", js_pci_setup },
#endif
#ifdef CONFIG_SCSI
	{ "max_scsi_luns=", scsi_luns_setup },
	{ "scsi_logging=", scsi_logging_setup },
#endif
#ifdef CONFIG_SCSI_ADVANSYS
	{ "advansys=", advansys_setup },
#endif
#if defined(CONFIG_BLK_DEV_HD)
	{ "hd=", hd_setup },
#endif
#ifdef CONFIG_CHR_DEV_ST
	{ "st=", st_setup },
#endif
#ifdef CONFIG_CHR_DEV_OSST
	{ "osst=", osst_setup },
#endif
#ifdef CONFIG_BUSMOUSE
	{ "bmouse=", bmouse_setup },
#endif
#ifdef CONFIG_MS_BUSMOUSE
	{ "msmouse=", msmouse_setup },
#endif
#ifdef CONFIG_SCSI_SEAGATE
	{ "st0x=", st0x_setup },
	{ "tmc8xx=", tmc8xx_setup },
#endif
#ifdef CONFIG_SCSI_T128
	{ "t128=", t128_setup },
#endif
#ifdef CONFIG_SCSI_PAS16
	{ "pas16=", pas16_setup },
#endif
#ifdef CONFIG_SCSI_GENERIC_NCR5380
	{ "ncr5380=", generic_NCR5380_setup },
	{ "ncr53c400=", generic_NCR53C400_setup },
	{ "ncr53c400a=", generic_NCR53C400A_setup },
	{ "dtc3181e=", generic_DTC3181E_setup },
#endif
#ifdef CONFIG_SCSI_AHA152X
	{ "aha152x=", aha152x_setup},
#endif
#ifdef CONFIG_SCSI_AHA1542
	{ "aha1542=", aha1542_setup},
#endif
#ifdef CONFIG_SCSI_GDTH
	{ "gdth=", gdth_setup},
#endif
#ifdef CONFIG_SCSI_AIC7XXX
	{ "aic7xxx=", aic7xxx_setup},
#endif
#ifdef CONFIG_SCSI_IPS
	{ "ips=", ips_setup},
#endif
#ifdef CONFIG_SCSI_BUSLOGIC
	{ "BusLogic=", BusLogic_Setup},
#endif
#ifdef CONFIG_SCSI_NCR53C8XX
	{ "ncr53c8xx=", ncr53c8xx_setup},
#endif
#ifdef CONFIG_SCSI_SYM53C8XX
	{ "sym53c8xx=", sym53c8xx_setup},
#endif
#ifdef CONFIG_SCSI_EATA
	{ "eata=", eata2x_setup},
#endif
#ifdef CONFIG_SCSI_U14_34F
	{ "u14-34f=", u14_34f_setup},
#endif
#ifdef CONFIG_SCSI_AM53C974
        { "AM53C974=", AM53C974_setup},
#endif
#ifdef CONFIG_SCSI_NCR53C406A
	{ "ncr53c406a=", NCR53c406a_setup},
#endif
#ifdef CONFIG_SCSI_SYM53C416
	{ "sym53c416=", sym53c416_setup},
#endif
#ifdef CONFIG_SCSI_FUTURE_DOMAIN
	{ "fdomain=", fdomain_setup},
#endif
#ifdef CONFIG_SCSI_IN2000
	{ "in2000=", in2000_setup},
#endif
#ifdef CONFIG_SCSI_7000FASST
	{ "wd7000=", wd7000_setup},
#endif
#ifdef CONFIG_SCSI_IBMMCA
        { "ibmmcascsi=", ibmmca_scsi_setup },
#endif
#ifdef CONFIG_SCSI_FD_MCS
	{ "fd_mcs=", fd_mcs_setup },
#endif
#if defined(CONFIG_SCSI_DC390T) && ! defined(CONFIG_SCSI_DC390T_NOGENSUPP)
        { "tmscsim=", dc390_setup },
#endif
#ifdef CONFIG_BLK_DEV_XD
	{ "xd=", xd_setup },
	{ "xd_geo=", xd_manual_geo_init },
#endif
#if defined(CONFIG_BLK_DEV_FD) || defined(CONFIG_AMIGA_FLOPPY) || defined(CONFIG_ATARI_FLOPPY)
	{ "floppy=", floppy_setup },
#endif
#ifdef CONFIG_BLK_DEV_PS2
	{ "eda=", ed_setup },
	{ "edb=", ed_setup },
	{ "tp720=", tp720_setup },
#endif
#ifdef CONFIG_CDU31A
	{ "cdu31a=", cdu31a_setup },
#endif CONFIG_CDU31A
#ifdef CONFIG_MCD
	{ "mcd=", mcd_setup },
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
	{ "mcdx=", mcdx_setup },
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
	{ "sbpcd=", sbpcd_setup },
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
	{ "aztcd=", aztcd_setup },
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
	{ "sonycd535=", sonycd535_setup },
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
	{ "gscd=", gscd_setup },
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
	{ "cm206=", cm206_setup },
#endif CONFIG_CM206
#ifdef CONFIG_OPTCD
	{ "optcd=", optcd_setup },
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
	{ "sjcd=", sjcd_setup },
#endif CONFIG_SJCD
#ifdef CONFIG_ISP16_CDI
	{ "isp16=", isp16_setup },
#endif CONFIG_ISP16_CDI
#ifdef CONFIG_SOUND_OSS
	{ "sound=", sound_setup },
#endif
#ifdef CONFIG_ISDN_DRV_ICN
	{ "icn=", icn_setup },
#endif
#ifdef CONFIG_ISDN_DRV_HISAX
       { "hisax=", HiSax_setup },
       { "HiSax=", HiSax_setup },
#endif
#ifdef CONFIG_ISDN_DRV_PCBIT
	{ "pcbit=", pcbit_setup },
#endif
#ifdef CONFIG_ATARIMOUSE
	{ "atamouse=", atari_mouse_setup },
#endif
#ifdef CONFIG_DMASOUND
	{ "dmasound=", dmasound_setup },
#endif
#ifdef CONFIG_ATARI_SCSI
	{ "atascsi=", atari_scsi_setup },
#endif
#ifdef CONFIG_STRAM_SWAP
	{ "stram_swap=", stram_swap_setup },
#endif
#if defined(CONFIG_A4000T_SCSI) || defined(CONFIG_WARPENGINE_SCSI) \
	    || defined(CONFIG_A4091_SCSI) || defined(CONFIG_MVME16x_SCSI) \
	    || defined(CONFIG_BVME6000_SCSI) \
	    || defined(CONFIG_BLZ603EPLUS_SCSI)
        { "53c7xx=", ncr53c7xx_setup },
#endif
#if defined(CONFIG_SCSI_SIM710)
	{ "sim710=", sim710_setup },
#endif
#if defined(CONFIG_A3000_SCSI) || defined(CONFIG_A2091_SCSI) \
	    || defined(CONFIG_GVP11_SCSI)
	{ "wd33c93=", wd33c93_setup },
#endif
#if defined(CONFIG_GVP11_SCSI)
	{ "gvp11=", gvp11_setup },
#endif
#ifdef CONFIG_MAC_SCSI
	{ "mac5380=", mac_scsi_setup },
#endif
#ifdef CONFIG_CYCLADES
	{ "cyclades=", cy_setup },
#endif
#ifdef CONFIG_DIGI
	{ "digi=", pcxx_setup },
#endif
#ifdef CONFIG_DIGIEPCA
	{ "digiepca=", epca_setup },
#endif
#ifdef CONFIG_RISCOM8
	{ "riscom8=", riscom8_setup },
#endif
#ifdef CONFIG_DMASCC
	{ "dmascc=", dmascc_setup },
#endif
#ifdef CONFIG_SPECIALIX
	{ "specialix=", specialix_setup },
#endif
#ifdef CONFIG_BAYCOM_PAR
	{ "baycom_par=", baycom_par_setup },
#endif
#ifdef CONFIG_BAYCOM_SER_FDX
	{ "baycom_ser_fdx=", baycom_ser_fdx_setup },
#endif
#ifdef CONFIG_BAYCOM_SER_HDX
	{ "baycom_ser_hdx=", baycom_ser_hdx_setup },
#endif
#ifdef CONFIG_SOUNDMODEM
	{ "soundmodem=", sm_setup },
#endif
#ifdef CONFIG_COMPUTONE
	{ "ip2=", ip2_setup },
#endif
#ifdef CONFIG_WDT
	{ "wdt=", wdt_setup },
#endif
#ifdef CONFIG_PARPORT
	{ "parport=", parport_setup },
#endif
#ifdef CONFIG_PLIP
	{ "plip=", plip_setup },
#endif
#ifdef CONFIG_HFMODEM
	{ "hfmodem=", hfmodem_setup },
#endif
#ifdef CONFIG_FTAPE
	{ "ftape=", ftape_setup},
#endif
#ifdef CONFIG_MD_BOOT
	{ "md=", md_setup},
#endif
#ifdef CONFIG_ADBMOUSE
	{ "adb_buttons=", adb_mouse_setup },
#endif
#ifdef CONFIG_LTPC
	{ "ltpc=", ltpc_setup },
#endif
#ifdef CONFIG_BLK_CPQ_DA
	{ "smart2=", cpqarray_setup },
#endif
	{ 0, 0 }
};

static struct kernel_param raw_params[] __initdata = {
	{ "root=", root_dev_setup },
#ifdef CONFIG_ROOT_NFS
	{ "nfsroot=", nfs_root_setup },
	{ "nfsaddrs=", ip_auto_config_setup },
#endif
#ifdef CONFIG_IP_PNP
	{ "ip=", ip_auto_config_setup },
#endif
#ifdef CONFIG_PCI
	{ "pci=", pci_setup },
#endif
#ifdef CONFIG_PARIDE_PD
	{ "pd.", pd_setup },
#endif
#ifdef CONFIG_PARIDE_PCD
	{ "pcd.", pcd_setup },
#endif
#ifdef CONFIG_PARIDE_PF
	{ "pf.", pf_setup },
#endif
#ifdef CONFIG_PARIDE_PT
        { "pt.", pt_setup },
#endif
#ifdef CONFIG_PARIDE_PG
        { "pg.", pg_setup },
#endif
#ifdef CONFIG_N2
	{ "n2=", n2_setup },
#endif
#ifdef CONFIG_C101
	{ "c101=", c101_setup },
#endif
#ifdef CONFIG_3215
	{ "condev=", con3215_setup },
#endif
#ifdef CONFIG_MDISK
        { "mdisk=", mdisk_setup },
#endif
#ifdef CONFIG_BLK_DEV_XPRAM
        { "xpram_parts=", xpram_setup },
#endif
#ifdef CONFIG_ARCH_S390
        { "vmhalt=", vmhalt_setup },
        { "vmpoff=", vmpoff_setup },
#endif
	{ 0, 0 }
};


#ifdef CONFIG_BLK_DEV_RAM
static void __init ramdisk_start_setup(char *str, int *ints)
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_image_start = ints[1];
}

static void __init load_ramdisk(char *str, int *ints)
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_doload = ints[1] & 3;
}

static void __init prompt_ramdisk(char *str, int *ints)
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_prompt = ints[1] & 1;
}

static void __init ramdisk_size(char *str, int *ints)
{
	if (ints[0] > 0 && ints[1] >= 0)
		rd_size = ints[1];
}
#endif

static int __init checksetup(char *line)
{
	struct new_kernel_param *p;
	int i, ints[11];

#ifdef CONFIG_BLK_DEV_IDE
	/* ide driver needs the basic string, rather than pre-processed values */
	if (!strncmp(line,"ide",3) || (!strncmp(line,"hd",2) && line[2] != '=')) {
		ide_setup(line);
		return 1;
	}
#endif
	for (i=0; raw_params[i].str; i++) {
		int n = strlen(raw_params[i].str);
		if (!strncmp(line,raw_params[i].str,n)) {
			raw_params[i].setup_func(line+n, NULL);
			return 1;
		}
	}
	for (i=0; cooked_params[i].str; i++) {
		int n = strlen(cooked_params[i].str);
		if (!strncmp(line,cooked_params[i].str,n)) {
			cooked_params[i].setup_func(get_options(line+n, ints), ints);
			return 1;
		}
	}

	/* Now handle new-style __setup parameters */
	p = &__setup_start;
	while (p < &__setup_end) {
		int n = strlen(p->str);
		if (!strncmp(line,p->str,n)) {
			if (p->setup_func(line+n))
				return 1;
		}
		p++;
	}
	return 0;
}

/* this should be approx 2 Bo*oMips to start (note initial shift), and will
   still work even if initially too large, it will just take slightly longer */
unsigned long loops_per_jiffy = (1<<12);

/* This is the number of bits of precision for the loops_per_jiffy.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

void __init calibrate_delay(void)
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;

	loops_per_jiffy = (1<<12);

	printk("Calibrating delay loop... ");
	while (loops_per_jiffy <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_jiffy);
		ticks = jiffies - ticks;
		if (ticks)
			break;
	}

/* Do a binary approximation to get loops_per_jiffy set to equal one clock
   (up to lps_precision bits) */
	loops_per_jiffy >>= 1;
	loopbit = loops_per_jiffy;
	while ( lps_precision-- && (loopbit >>= 1) ) {
		loops_per_jiffy |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		__delay(loops_per_jiffy);
		if (jiffies != ticks)	/* longer than 1 tick */
			loops_per_jiffy &= ~loopbit;
	}

/* Round the value and print it */	
	printk("%lu.%02lu BogoMIPS\n",
		loops_per_jiffy/(500000/HZ),
		(loops_per_jiffy/(5000/HZ)) % 100);
}

/*
 * This is a simple kernel command line parsing function: it parses
 * the command line, and fills in the arguments/environment to init
 * as appropriate. Any cmd-line option is taken to be an environment
 * variable if it contains the character '='.
 *
 * This routine also checks for options meant for the kernel.
 * These options are not given to init - they are for internal kernel use only.
 */
static void __init parse_options(char *line)
{
	char *next;
        char *quote;
	int args, envs;

	if (!*line)
		return;
	args = 0;
	envs = 1;	/* TERM is set to 'linux' by default */
	next = line;
	while ((line = next) != NULL) {
		/* On S/390 we want to be able to pass an options that
                 * contains blanks. For example vmhalt="IPL CMS". 
                 * To allow that I added code that prevents blanks in
                 * quotes to be recognized as delimiter. -- Martin
                 */
                quote = strchr(line,'"');
                next = strchr(line, ' ');
                while (next != NULL && quote != NULL && quote < next) {
                        /* we found a left quote before the next blank
                         * now we have to find the matching right quote
                         */
                        next = strchr(quote+1, '"');
                        if (next != NULL) {
                                quote = strchr(next+1, '"');
                                next = strchr(next+1, ' ');
                        }
                }
                if (next != NULL)
                        *next++ = 0;
		/*
		 * check for kernel options first..
		 */
		if (!strcmp(line,"ro")) {
			root_mountflags |= MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"rw")) {
			root_mountflags &= ~MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"debug")) {
			console_loglevel = 10;
			continue;
		}
		if (!strcmp(line,"quiet")) {
			console_loglevel = 4;
			continue;
		}
		if (!strncmp(line,"init=",5)) {
			line += 5;
			execute_command = line;
			/* In case LILO is going to boot us with default command line,
			 * it prepends "auto" before the whole cmdline which makes
			 * the shell think it should execute a script with such name.
			 * So we ignore all arguments entered _before_ init=... [MJ]
			 */
			args = 0;
			continue;
		}
		if (checksetup(line))
			continue;
		
		/*
		 * Then check if it's an environment variable or
		 * an option.
		 */
		if (strchr(line,'=')) {
			if (envs >= MAX_INIT_ENVS)
				break;
			envp_init[++envs] = line;
		} else {
			if (args >= MAX_INIT_ARGS)
				break;
			argv_init[++args] = line;
		}
	}
	argv_init[args+1] = NULL;
	envp_init[envs+1] = NULL;
}


extern void setup_arch(char **, unsigned long *, unsigned long *);

#ifndef __SMP__

/*
 *	Uniprocessor idle thread
 */
 
int cpu_idle(void *unused)
{
	for(;;)
		idle();
}

#define smp_init()	do { } while (0)

#else

/*
 *	Multiprocessor idle thread is in arch/...
 */
 
extern int cpu_idle(void * unused);

/* Called by boot processor to activate the rest. */
static void __init smp_init(void)
{
	/* Get other processors into their bootup holding patterns. */
	smp_boot_cpus();
	smp_threads_ready=1;
	smp_commence();
}		

#endif

extern void initialize_secondary(void);

/*
 *	Activate the first processor.
 */
 
asmlinkage void __init start_kernel(void)
{
	char * command_line;

#ifdef __SMP__
	static int boot_cpu = 1;
	/* "current" has been set up, we need to load it now */
	if (!boot_cpu)
		initialize_secondary();
	boot_cpu = 0;
#endif

/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	printk(linux_banner);
	setup_arch(&command_line, &memory_start, &memory_end);
	memory_start = paging_init(memory_start,memory_end);
	trap_init();
        memory_start = init_IRQ( memory_start );
	sched_init();
	time_init();
	parse_options(command_line);

	/*
	 * HACK ALERT! This is early. We're enabling the console before
	 * we've done PCI setups etc, and console_init() must be aware of
	 * this. But we do want output early, in case something goes wrong.
	 */
	memory_start = console_init(memory_start,memory_end);
#ifdef CONFIG_MODULES
	init_modules();
#endif
	if (prof_shift) {
		prof_buffer = (unsigned int *) memory_start;
		/* only text is profiled */
		prof_len = (unsigned long) &_etext - (unsigned long) &_stext;
		prof_len >>= prof_shift;
		memory_start += prof_len * sizeof(unsigned int);
		memset(prof_buffer, 0, prof_len * sizeof(unsigned int));
	}

	memory_start = kmem_cache_init(memory_start, memory_end);
	sti();
	calibrate_delay();
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok && initrd_start < memory_start) {
		printk(KERN_CRIT "initrd overwritten (0x%08lx < 0x%08lx) - "
		    "disabling it.\n",initrd_start,memory_start);
		initrd_start = 0;
	}
#endif
	mem_init(memory_start,memory_end);
	kmem_cache_sizes_init();
#ifdef CONFIG_3215_CONSOLE
        con3215_activate();
#endif
#ifdef CONFIG_PROC_FS
	proc_root_init();
#endif
	uidcache_init();
	filescache_init();
	dcache_init();
	vma_init();
	buffer_init(memory_end-memory_start);
 	page_cache_init(memory_end-memory_start);
#ifdef CONFIG_ARCH_S390
	ccwcache_init();
#endif
	signals_init();
	inode_init();
	file_table_init();
#if defined(CONFIG_SYSVIPC)
	ipc_init();
#endif
#if defined(CONFIG_QUOTA)
	dquot_init_hash();
#endif
	check_bugs();
	printk("POSIX conformance testing by UNIFIX\n");

	/* 
	 *	We count on the initial thread going ok 
	 *	Like idlers init is an unlocked kernel thread, which will
	 *	make syscalls (and thus be locked).
	 */
	smp_init();
	kernel_thread(init, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	current->need_resched = 1;
 	cpu_idle(NULL);
}

#ifdef CONFIG_BLK_DEV_INITRD
static int do_linuxrc(void * shell)
{
	static char *argv[] = { "linuxrc", NULL, };

	close(0);close(1);close(2);
	setsid();
	(void) open("/dev/console",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	return execve(shell, argv, envp_init);
}

static void __init no_initrd(char *s,int *ints)
{
	mount_initrd = 0;
}
#endif

struct task_struct *child_reaper = &init_task;


static void __init do_initcalls(void)
{
	initcall_t *call;

	call = &__initcall_start;
	
	while (call < &__initcall_end)
	{
		(*call)();
		call++;
	} 
}


/*
 * Ok, the machine is now initialized. None of the devices
 * have been touched yet, but the CPU subsystem is up and
 * running, and memory and process management works.
 *
 * Now we can finally start doing some real work..
 */
static void __init do_basic_setup(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	int real_root_mountflags;
#endif

	/*
	 * Tell the world that we're going to be the grim
	 * reaper of innocent orphaned children.
	 *
	 * We don't want people to have to make incorrect
	 * assumptions about where in the task array this
	 * can be found.
	 */
	child_reaper = current;

#if defined(CONFIG_MTRR)	/* Do this after SMP initialization */
/*
 * We should probably create some architecture-dependent "fixup after
 * everything is up" style function where this would belong better
 * than in init/main.c..
 */
	mtrr_init();
#endif

#ifdef CONFIG_SYSCTL
	sysctl_init();
#endif

	/*
	 * Ok, at this point all CPU's should be initialized, so
	 * we can start looking into devices..
	 */
#ifdef CONFIG_ARCH_S390
	s390_init_machine_check();
#endif

#ifdef CONFIG_PCI
	pci_init();
#endif
#ifdef CONFIG_SBUS
	sbus_init();
#endif
#if defined(CONFIG_PPC)
	ppc_init();
#endif
#ifdef CONFIG_MCA
	mca_init();
#endif
#ifdef CONFIG_ARCH_ACORN
	ecard_init();
#endif
#ifdef CONFIG_ZORRO
	zorro_init();
#endif
#ifdef CONFIG_DIO
	dio_init();
#endif
#ifdef CONFIG_MAC
	nubus_init();
#endif
#ifdef CONFIG_AGP
	agp_init();
#endif

	/* Networking initialization needs a process context */ 
	sock_init();

	/* Launch bdflush from here, instead of the old syscall way. */
	kernel_thread(bdflush, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	kernel_thread(kupdate, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	/* Start the background pageout daemon. */
	kswapd_setup();
	kernel_thread(kswapd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);

#if CONFIG_AP1000
	/* Start the async paging daemon. */
	{
	  extern int asyncd(void *);	 
	  kernel_thread(asyncd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	}
#endif

#ifdef CONFIG_BLK_DEV_INITRD

	real_root_dev = ROOT_DEV;
	real_root_mountflags = root_mountflags;
	if (initrd_start && mount_initrd) root_mountflags &= ~MS_RDONLY;
	else mount_initrd =0;
#endif

	/* Start the event daemon thread */
	start_context_thread();

	/* Set up devices .. */
	device_setup();

	/* .. executable formats .. */
	binfmt_setup();

	/* the functions marked initcall  */
	
	do_initcalls();

	/* .. filesystems .. */
	filesystem_setup();

#ifdef CONFIG_IRDA
	irda_device_init(); /* Must be done after protocol initialization */
#endif

#ifdef CONFIG_MICROCODE
	microcode_init();
#endif

	/* Mount the root filesystem.. */
	mount_root();

#ifdef CONFIG_BLK_DEV_INITRD
	root_mountflags = real_root_mountflags;
	if (mount_initrd && ROOT_DEV != real_root_dev
	    && MAJOR(ROOT_DEV) == RAMDISK_MAJOR && MINOR(ROOT_DEV) == 0) {
		int error;
		int i, pid;

		pid = kernel_thread(do_linuxrc, "/linuxrc", SIGCHLD);
		if (pid>0)
			while (pid != wait(&i));
		if (MAJOR(real_root_dev) != RAMDISK_MAJOR
		     || MINOR(real_root_dev) != 0) {
			error = change_root(real_root_dev,"/initrd");
			if (error)
				printk(KERN_ERR "Change root to /initrd: "
				    "error %d\n",error);
		}
	}
#endif
}

static int init(void * unused)
{
	lock_kernel();
	do_basic_setup();

	/*
	 * Ok, we have completed the initial bootup, and
	 * we're essentially up and running. Get rid of the
	 * initmem segments and start the user-mode stuff..
	 */
	free_initmem();
	unlock_kernel();

	if (open("/dev/console", O_RDWR, 0) < 0)
		printk("Warning: unable to open an initial console.\n");

	(void) dup(0);
	(void) dup(0);
	
	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are 
	 * trying to recover a really broken machine.
	 */

	if (execute_command)
		execve(execute_command,argv_init,envp_init);
	execve("/sbin/init",argv_init,envp_init);
	execve("/etc/init",argv_init,envp_init);
	execve("/bin/init",argv_init,envp_init);
	execve("/bin/sh",argv_init,envp_init);
	panic("No init found.  Try passing init= option to kernel.");
}
