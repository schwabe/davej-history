#ifdef MODVERSIONS
#undef  CONFIG_MODVERSIONS
#define CONFIG_MODVERSIONS
#ifndef _set_ver
#define _set_ver(sym,vers) sym ## _R ## vers
#endif
#include <linux/modules/b1capi.ver>
#include <linux/modules/b1pci.ver>
#include <linux/modules/capidrv.ver>
#include <linux/modules/capiutil.ver>
#include <linux/modules/fatfs_syms.ver>
#include <linux/modules/firewall.ver>
#include <linux/modules/isdn_syms.ver>
#include <linux/modules/ksyms.ver>
#include <linux/modules/md.ver>
#include <linux/modules/misc.ver>
#include <linux/modules/msdosfs_syms.ver>
#include <linux/modules/netsyms.ver>
#include <linux/modules/nls.ver>
#include <linux/modules/p8022.ver>
#include <linux/modules/p8022tr.ver>
#include <linux/modules/ppp.ver>
#include <linux/modules/procfs_syms.ver>
#include <linux/modules/psnap.ver>
#include <linux/modules/scsi_syms.ver>
#include <linux/modules/serial.ver>
#include <linux/modules/slhc.ver>
#include <linux/modules/vfatfs_syms.ver>
#undef  CONFIG_MODVERSIONS
#endif
