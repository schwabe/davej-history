/*
 *  arch/s390/kernel/s390_ksyms.c
 *
 *  S390 version
 */
#include <linux/config.h>
#include <linux/module.h>
#include <asm/debug.h>
#include <linux/genhd.h>
#include <asm/ccwcache.h>
#include <asm/irq.h>
#include <asm/string.h>
#include <asm/checksum.h>
#include <asm/s390_ext.h>
#include <asm/s390dyn.h>
#include <asm/ebcdic.h>
#include <asm/timex.h>
#include <asm/delay.h>
#if CONFIG_CHANDEV
#include <asm/chandev.h>
#endif
#if CONFIG_IP_MULTICAST
#include <net/arp.h>
#endif

/*
 * I/O subsystem
 */
EXPORT_SYMBOL(halt_IO);
EXPORT_SYMBOL(do_IO);
EXPORT_SYMBOL(resume_IO);
EXPORT_SYMBOL(ioinfo);
EXPORT_SYMBOL(get_dev_info_by_irq);
EXPORT_SYMBOL(get_dev_info_by_devno);
EXPORT_SYMBOL(get_irq_by_devno);
EXPORT_SYMBOL(get_devno_by_irq);
EXPORT_SYMBOL(get_irq_first);
EXPORT_SYMBOL(get_irq_next);
EXPORT_SYMBOL(read_conf_data);
EXPORT_SYMBOL(read_dev_chars);
EXPORT_SYMBOL(s390_request_irq_special);
EXPORT_SYMBOL(s390_device_register);
EXPORT_SYMBOL(s390_device_unregister);

EXPORT_SYMBOL(ccw_alloc_request);
EXPORT_SYMBOL(ccw_free_request);

EXPORT_SYMBOL(register_external_interrupt);
EXPORT_SYMBOL(unregister_external_interrupt);

/*
 * debug feature
 */
EXPORT_SYMBOL(debug_register);
EXPORT_SYMBOL(debug_unregister); 
EXPORT_SYMBOL(debug_event);
EXPORT_SYMBOL(debug_text_event);
EXPORT_SYMBOL(debug_exception);
EXPORT_SYMBOL(debug_text_exception);

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

EXPORT_SYMBOL_NOVERS(_ascebc_500);
EXPORT_SYMBOL_NOVERS(_ebcasc_500);
EXPORT_SYMBOL_NOVERS(_ascebc);
EXPORT_SYMBOL_NOVERS(_ebcasc);
EXPORT_SYMBOL_NOVERS(_ebc_tolower);
EXPORT_SYMBOL_NOVERS(_ebc_toupper);

/*
 * misc.
 */
EXPORT_SYMBOL(__udelay);
#ifdef CONFIG_SMP
#include <asm/smplock.h>
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);
EXPORT_SYMBOL(global_bh_lock);
EXPORT_SYMBOL(synchronize_bh);
EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(smp_ctl_set_bit);
EXPORT_SYMBOL(smp_ctl_clear_bit);
#endif
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(csum_fold);
EXPORT_SYMBOL(genhd_dasd_name);

#if CONFIG_IP_MULTICAST
/* Required for lcs gigibit ethernet multicast support */
EXPORT_SYMBOL(arp_mc_map);
#endif
