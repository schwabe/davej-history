/*
 *  include/asm-s390/mmu_context.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/mmu_context.h"
 */

#ifndef __S390_MMU_CONTEXT_H
#define __S390_MMU_CONTEXT_H

/*
 * get a new mmu context.. S390 don't know about contexts.
 */
#define get_mmu_context(x) do { } while (0)

#define init_new_context(mm)	do { } while(0)
#define destroy_context(mm)	do { } while(0)
#define activate_context(tsk)	do { } while(0)

#endif
