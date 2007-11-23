/*
 *  arch/s390/kernel/s390_ksyms.c
 *
 *  S390 version
 */
#include <linux/module.h>
#include <linux/genhd.h>
#include <asm/checksum.h>
#include <asm/delay.h>
#if CONFIG_IP_MULTICAST
#include <net/arp.h>
#endif

/*
 * memory management
 */
EXPORT_SYMBOL(init_mm);
EXPORT_SYMBOL(_oi_bitmap);
EXPORT_SYMBOL(_ni_bitmap);
EXPORT_SYMBOL(_zb_findmap);

/*
 * string functions
 */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strncpy);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strtok);
EXPORT_SYMBOL_NOVERS(strpbrk);
EXPORT_SYMBOL_NOVERS(strstr);

/*
 * misc.
 */
EXPORT_SYMBOL(__copy_from_user_fixup);
EXPORT_SYMBOL(__copy_to_user_fixup);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(csum_fold);
EXPORT_SYMBOL(genhd_dasd_name);

#ifdef CONFIG_IP_MULTICAST
/* Required for lcs gigibit ethernet multicast support */
EXPORT_SYMBOL(arp_mc_map);
#endif
