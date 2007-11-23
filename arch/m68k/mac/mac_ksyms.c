#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/traps.h>

/* Hook for mouse driver */
extern void (*adb_mouse_interrupt_hook) (char *);

/* Says whether we're using A/UX interrupts or not */
extern int via_alt_mapping;

#ifndef CONFIG_ADB_NEW
EXPORT_SYMBOL(adb_mouse_interrupt_hook);
#endif
EXPORT_SYMBOL(via_alt_mapping);
