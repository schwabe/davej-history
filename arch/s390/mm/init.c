/*
 *  arch/s390/mm/init.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/lowcore.h>

static int test_access(unsigned long loc)
{
	static const int ssm_mask = 0x07000000L;
	int rc, i;

        rc = 0;
	for (i=0; i<4; i++) {
		__asm__ __volatile__(
                        "    slr   %0,%0\n"
			"    ssm   %1\n"
			"    tprot 0(%2),0\n"
			"0:  jne   1f\n"
			"    lhi   %0,1\n"
			"1:  ssm   %3\n"
                        ".section __ex_table,\"a\"\n"
                        "   .align 4\n"
                        "   .long  0b,1b\n"
                        ".previous"
			: "+&d" (rc) : "i" (0), "a" (loc), "m" (ssm_mask)
			: "cc");
		if (rc == 0)
			break;
		loc += 0x100000;
	}
	return rc;
}

extern void show_net_buffers(void);

extern unsigned long initrd_start,initrd_end;

static inline void invalidate_page(pte_t *pte)
{
        int i;
        for (i=0;i<PTRS_PER_PTE;i++)
                pte_clear(pte++);
}

void __bad_pte_kernel(pmd_t *pmd)
{
        printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
        pmd_val(*pmd) = _KERNPG_TABLE + __pa(BAD_PAGETABLE);
}

void __bad_pte(pmd_t *pmd)
{
        printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
        pmd_val(*pmd) = _PAGE_TABLE + __pa(BAD_PAGETABLE);
}

pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long offset)
{
        pte_t *pte;

        pte = (pte_t *) __get_free_page(GFP_KERNEL);
        if (pmd_none(*pmd)) {
                if (pte) {
                        invalidate_page(pte);
                        pmd_val(pmd[0]) = _KERNPG_TABLE + __pa(pte);
                        pmd_val(pmd[1]) = _KERNPG_TABLE + __pa(pte)+1024;
                        pmd_val(pmd[2]) = _KERNPG_TABLE + __pa(pte)+2048;
                        pmd_val(pmd[3]) = _KERNPG_TABLE + __pa(pte)+3072;
                        return pte + offset;
                }
                pmd_val(pmd[0]) = _KERNPG_TABLE + __pa(BAD_PAGETABLE);
                pmd_val(pmd[1]) = _KERNPG_TABLE + __pa(BAD_PAGETABLE)+1024;
                pmd_val(pmd[2]) = _KERNPG_TABLE + __pa(BAD_PAGETABLE)+2048;
                pmd_val(pmd[3]) = _KERNPG_TABLE + __pa(BAD_PAGETABLE)+3072;
                return NULL;
        }
        free_page((unsigned long)pte);
        if (pmd_bad(*pmd)) {
                __bad_pte_kernel(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + offset;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
        unsigned long pte;

        pte = (unsigned long) __get_free_page(GFP_KERNEL);
        if (pmd_none(*pmd)) {
                if (pte) {
                        invalidate_page((pte_t*) pte);
                        pmd_val(pmd[0]) = _PAGE_TABLE + __pa(pte);
                        pmd_val(pmd[1]) = _PAGE_TABLE + __pa(pte)+1024;
                        pmd_val(pmd[2]) = _PAGE_TABLE + __pa(pte)+2048;
                        pmd_val(pmd[3]) = _PAGE_TABLE + __pa(pte)+3072;
                        return (pte_t *) pte + offset;
                }
                pmd_val(pmd[0]) = _PAGE_TABLE + __pa(BAD_PAGETABLE);
                pmd_val(pmd[1]) = _PAGE_TABLE + __pa(BAD_PAGETABLE)+1024;
                pmd_val(pmd[2]) = _PAGE_TABLE + __pa(BAD_PAGETABLE)+2048;
                pmd_val(pmd[3]) = _PAGE_TABLE + __pa(BAD_PAGETABLE)+3072;
                return NULL;
        }
        free_page(pte);
        if (pmd_bad(*pmd)) {
                __bad_pte(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + offset;
}

int do_check_pgt_cache(int low, int high)
{
        int freed = 0;
        if(pgtable_cache_size > high) {
                do {
                        if(pgd_quicklist)
                                free_pgd_slow(get_pgd_fast()), freed += 2;
                        if(pmd_quicklist)
                                free_pmd_slow(get_pmd_fast()), freed++;
                        if(pte_quicklist)
                                free_pte_slow(get_pte_fast()), freed++;
                } while(pgtable_cache_size > low);
        }
        return freed;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pte_t * __bad_pagetable(void)
{
        extern char empty_bad_page_table[PAGE_SIZE];

	memset((void *)empty_bad_page_table, 0, PAGE_SIZE);
        return (pte_t *) empty_bad_page_table;
}

pte_t __bad_page(void)
{
        extern char empty_bad_page[PAGE_SIZE];

	memset((void *)empty_bad_page, 0, PAGE_SIZE);
        return pte_mkdirty(mk_pte((unsigned long) empty_bad_page, PAGE_SHARED));
}

void show_mem(void)
{
        int i,free = 0,total = 0,reserved = 0;
        int shared = 0, cached = 0;

        printk("Mem-info:\n");
        show_free_areas();
        printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
        i = max_mapnr;
        while (i-- > 0) {
                total++;
                if (PageReserved(mem_map+i))
                        reserved++;
                else if (PageSwapCache(mem_map+i))
                        cached++;
                else if (!atomic_read(&mem_map[i].count))
                        free++;
                else
                        shared += atomic_read(&mem_map[i].count) - 1;
        }
        printk("%d pages of RAM\n",total);
        printk("%d reserved pages\n",reserved);
        printk("%d pages shared\n",shared);
        printk("%d pages swap cached\n",cached);
        printk("%ld pages in page table cache\n",pgtable_cache_size);
        show_buffers();
#ifdef CONFIG_NET
        show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/* References to section boundaries */

extern unsigned long _text;
extern unsigned long _etext;
extern unsigned long _edata;
extern unsigned long __bss_start;
extern unsigned long _end;

extern unsigned long __init_begin;
extern unsigned long __init_end;

/*
 * the initial mapping is set up by linload to address of  4 MB
 * to enable virtual addressing to the first 4 MB
 * paging_init will erase this initial mapping
 */

pgd_t         swapper_pg_dir[512]   __attribute__ ((__aligned__ (4096)));
unsigned long empty_bad_page[1024] __attribute__ ((__aligned__ (4096)));
unsigned long empty_zero_page[1024] __attribute__ ((__aligned__ (4096)));
pte_t         empty_bad_page_table[1024] __attribute__ ((__aligned__ (4096)));

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
__initfunc(unsigned long paging_init(unsigned long start_mem, unsigned long end_mem))
{
        pgd_t * pg_dir;
        pte_t * pg_table;
        pte_t   pte;
	int     i;
        unsigned long tmp;
        unsigned long address=0;
        unsigned long pgdir_k = (__pa(swapper_pg_dir) & PAGE_MASK) | _KERNSEG_TABLE;
        static const int ssm_mask = 0x04000000L;

	/* unmap whole virtual address space */

        pg_dir = swapper_pg_dir;

	for (i=0;i<KERNEL_PGD_PTRS;i++) 
	        pmd_clear((pmd_t*)pg_dir++);

	/*
	 * map whole physical memory to virtual memory (identity mapping) 
	 */

        start_mem = PAGE_ALIGN(start_mem);
        pg_dir = swapper_pg_dir;

        while (address < end_mem) {
                /*
                 * pg_table is physical at this point
                 */
                pg_table = (pte_t *) __pa(start_mem);


                pg_dir->pgd0 =  (_PAGE_TABLE | ((unsigned long) pg_table));
                pg_dir->pgd1 =  (_PAGE_TABLE | ((unsigned long) pg_table+1024));
                pg_dir->pgd2 =  (_PAGE_TABLE | ((unsigned long) pg_table+2048));
                pg_dir->pgd3 =  (_PAGE_TABLE | ((unsigned long) pg_table+3072));
                pg_dir++;

                /* now change pg_table to kernel virtual addresses */
                pg_table = (pte_t *) start_mem;
                start_mem += PAGE_SIZE;

                for (tmp = 0 ; tmp < PTRS_PER_PTE ; tmp++,pg_table++) {
                        pte = mk_pte(address, PAGE_KERNEL);
                        if (address >= end_mem)
                                pte_clear(&pte);
                        set_pte(pg_table, pte);
                        address += PAGE_SIZE;
                }
        }

        /* enable virtual mapping in kernel mode */
        __asm__ __volatile__("    LCTL  1,1,%0\n"
                             "    LCTL  7,7,%0\n"
                             "    LCTL  13,13,%0\n"
                             "    SSM   %1"
                             : : "m" (pgdir_k), "m" (ssm_mask));

        local_flush_tlb();

        return free_area_init(start_mem, end_mem);
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
        unsigned long start_low_mem = PAGE_SIZE;
        int codepages = 0;
        int reservedpages = 0;
        int datapages = 0;
        int initpages = 0;
        unsigned long tmp;

        end_mem &= PAGE_MASK;
        high_memory = (void *) end_mem;
        max_mapnr = num_physpages = MAP_NR(end_mem);

        /* clear the zero-page */
        memset(empty_zero_page, 0, PAGE_SIZE);

        /* mark usable pages in the mem_map[] */
        start_low_mem = PAGE_ALIGN(start_low_mem)+PAGE_OFFSET;

#if 0 /* FIXME: WHATS THAT FOR ?!?! */
#ifdef __SMP__
        /*
         * But first pinch a few for the stack/trampoline stuff
         *      FIXME: Don't need the extra page at 4K, but need to fix
         *      trampoline before removing it. (see the GDT stuff)
         *
         */
        start_low_mem += PAGE_SIZE;                   /* 32bit startup code*/
        start_low_mem = smp_alloc_memory(start_low_mem);/* AP processor stacks*/
#endif
#endif
        start_mem = PAGE_ALIGN(start_mem);

        tmp = start_mem;
        while (tmp < end_mem) {
                if (tmp && (tmp & 0x3ff000) == 0 &&
                    test_access(tmp) == 0) {
                        int i;
                        printk("4M Segment %lX not available\n",tmp);
                        for (i = 0;i<0x400;i++) {
                                set_bit(PG_reserved,
                                        &mem_map[MAP_NR(tmp)].flags);
                                tmp += PAGE_SIZE;
                        }
                } else {
                        clear_bit(PG_reserved, &mem_map[MAP_NR(tmp)].flags);
                        tmp += PAGE_SIZE;
                }
        }
        for (tmp = PAGE_OFFSET ; tmp < end_mem ; tmp += PAGE_SIZE) {
                if (PageReserved(mem_map+MAP_NR(tmp))) {
                        if (tmp >= (unsigned long) &_text && tmp < (unsigned long) &_edata) {
                                if (tmp < (unsigned long) &_etext) {
#if 0
                                        if (tmp >= 0x00001000UL) {
                                                pgd_t *pgd = pgd_offset_k(tmp);
                                                pmd_t *pmd = pmd_offset(pgd, tmp);
                                                pte_t *pte = pte_offset(pmd, tmp);

                                                *pte = pte_wrprotect(*pte);
                                        }
#endif
                                        codepages++;
                                } else
                                        datapages++;
                        } else if (tmp >= (unsigned long) __init_begin
                                   && tmp < (unsigned long) __init_end)
                                initpages++;
                        else if (tmp >= (unsigned long) &__bss_start
                                 && tmp < (unsigned long) start_mem)
                                datapages++;
                        else
                                reservedpages++;
                        continue;
                }
                atomic_set(&mem_map[MAP_NR(tmp)].count, 1);

#ifdef CONFIG_BLK_DEV_INITRD
                if (!initrd_start || (tmp < initrd_start || tmp >=
                    initrd_end))
#endif
                        free_page(tmp);
        }
        printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
                (unsigned long) nr_free_pages << (PAGE_SHIFT-10),
                max_mapnr << (PAGE_SHIFT-10),
                codepages << (PAGE_SHIFT-10),
                reservedpages << (PAGE_SHIFT-10),
                datapages << (PAGE_SHIFT-10),
                initpages << (PAGE_SHIFT-10));

}

void free_initmem(void)
{
        unsigned long addr;

        addr = (unsigned long)(&__init_begin);
        for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
                mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
                atomic_set(&mem_map[MAP_NR(addr)].count, 1);
                free_page(addr);
        }
        printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
        int i;

        i = max_mapnr;
        val->totalram = 0;
        val->sharedram = 0;
        val->freeram = nr_free_pages << PAGE_SHIFT;
        val->bufferram = buffermem;
        while (i-- > 0)  {
                if (PageReserved(mem_map+i))
                        continue;
                val->totalram++;
                if (!atomic_read(&mem_map[i].count))
                        continue;
                val->sharedram += atomic_read(&mem_map[i].count) - 1;
        }
        val->totalram <<= PAGE_SHIFT;
        val->sharedram <<= PAGE_SHIFT;
        return;
}
