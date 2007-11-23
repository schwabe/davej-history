/*
 *  arch/s390/kernel/s390ksyms.c
 *
 *  S390 version
 */

#include "irq.h"
#include "asm/string.h"

EXPORT_SYMBOL(get_dev_info_by_irq);
EXPORT_SYMBOL(get_dev_info_by_devno);
EXPORT_SYMBOL(get_irq_by_devno);
EXPORT_SYMBOL(get_devno_by_irq);
EXPORT_SYMBOL(halt_IO);
EXPORT_SYMBOL(irq_desc);
EXPORT_SYMBOL(do_IO);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(init_mm);
EXPORT_SYMBOL(_oi_bitmap);
EXPORT_SYMBOL(_ni_bitmap);
EXPORT_SYMBOL(_zb_findmap);

