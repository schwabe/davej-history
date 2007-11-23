/*
 *  include/asm-s390/pgtable.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner
 *
 *  Derived from "include/asm-i386/pgtable.h"
 */

#ifndef _ASM_S390_PGTABLE_H
#define _ASM_S390_PGTABLE_H

#include <linux/config.h>

/*
 * The Linux memory management assumes a three-level page table setup. On
 * the S390, we use that, but "fold" the mid level into the top-level page
 * table, so that we physically have the same two-level page table as the
 * S390 mmu expects.
 *
 * This file contains the functions and defines necessary to modify and use
 * the S390 page table tree.
 */
#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/tasks.h>

/* Caches aren't brain-dead on S390. */
#define flush_cache_all()                       do { } while (0)
#define flush_cache_mm(mm)                      do { } while (0)
#define flush_cache_range(mm, start, end)       do { } while (0)
#define flush_cache_page(vma, vmaddr)           do { } while (0)
#define flush_page_to_ram(page)                 do { } while (0)
#define flush_icache_range(start, end)          do { } while (0)
#define flush_dcache_page(page)			do { } while (0)

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs 
 *    called only from vmalloc/vfree
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 */


/*
 * s390 has two ways of flushing TLBs
 * 'ptlb' does a flush of the local processor
 * 'ipte' invalidates a pte in a page table and flushes that out of 
 * the TLBs of all PUs of a SMP 
 */

#define __flush_tlb() \
do {  __asm__ __volatile__("ptlb": : :"memory"); } while (0)


static inline void __flush_global_tlb(void) 
{
	int cs1=0,dum=0;
	int *adr;
	long long dummy=0;
	adr = (int*) (((int)(((int*) &dummy)+1) & 0xfffffffc)|1);
	__asm__ __volatile__("lr    2,%0\n\t"
			     "lr    3,%1\n\t"
			     "lr    4,%2\n\t"
			     ".long 0xb2500024" :
			     : "d" (cs1), "d" (dum), "d" (adr)
			     : "2", "3", "4");
}

static inline void __flush_tlb_one(struct mm_struct *mm,
                                   unsigned long addr);


#ifndef __SMP__

#define flush_tlb()       __flush_tlb()
#define flush_tlb_all()   __flush_tlb()
#define local_flush_tlb() __flush_tlb()

/*
 * We always need to flush, since s390 does not flush tlb
 * on each context switch
 */


static inline void flush_tlb_mm(struct mm_struct *mm)
{
        __flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
        unsigned long addr)
{
        __flush_tlb_one(vma->vm_mm,addr);
}

static inline void flush_tlb_range(struct mm_struct *mm,
        unsigned long start, unsigned long end)
{
        __flush_tlb();
}

#else

/*
 * We aren't very clever about this yet -  SMP could certainly
 * avoid some global flushes..
 */

#include <asm/smp.h>

#define local_flush_tlb() \
        __flush_tlb()

/*
 *      We only have to do global flush of tlb if process run since last
 *      flush on any other pu than current. 
 *      If we have threads (mm->count > 1) we always do a global flush, 
 *      since the process runs on more than one processor at the same time.
 */

static inline void flush_tlb_current_task(void)
{
	if ((atomic_read(&current->mm->count) != 1) ||
	    (current->mm->cpu_vm_mask != (1UL << smp_processor_id()))) {
		current->mm->cpu_vm_mask = (1UL << smp_processor_id());
		__flush_global_tlb();
	} else {                 
		local_flush_tlb();
	}
}

#define flush_tlb() flush_tlb_current_task()

#define flush_tlb_all() __flush_global_tlb()

static inline void flush_tlb_mm(struct mm_struct * mm)
{
	if ((atomic_read(&mm->count) != 1) ||
	    (mm->cpu_vm_mask != (1UL << smp_processor_id()))) {
		mm->cpu_vm_mask = (1UL << smp_processor_id());
		__flush_global_tlb();
	} else {                 
		local_flush_tlb();
	}
}

static inline void flush_tlb_page(struct vm_area_struct * vma,
        unsigned long va)
{
	__flush_tlb_one(vma->vm_mm,va);
}

static inline void flush_tlb_range(struct mm_struct * mm,
				   unsigned long start, unsigned long end)
{
	if ((atomic_read(&mm->count) != 1) ||
	    (mm->cpu_vm_mask != (1UL << smp_processor_id()))) {
		mm->cpu_vm_mask = (1UL << smp_processor_id());
		__flush_global_tlb();
	} else {                 
		local_flush_tlb();
	}
}

#endif
#endif                                 /* !__ASSEMBLY__                    */


/* Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
#define set_pte(pteptr, pteval) ((*(pteptr)) = (pteval))

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT       22
#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT     22
#define PGDIR_SIZE      (1UL << PGDIR_SHIFT)
#define PGDIR_MASK      (~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the S390 is two-level, so
 * we don't really have any PMD directory physically.
 * for S390 segment-table entries are combined to one PGD
 * that leads to 1024 pte per pgd
 */
#define PTRS_PER_PTE    1024
#define PTRS_PER_PMD    1
#define PTRS_PER_PGD    512


/*
 * pgd entries used up by user/kernel:
 */
#define USER_PTRS_PER_PGD  512
#define USER_PGD_PTRS      512
#define KERNEL_PGD_PTRS    512
#ifndef __ASSEMBLY__
/* Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
#define VMALLOC_OFFSET  (8*1024*1024)
#define VMALLOC_START   (((unsigned long) high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END     (0x7fffffffL)


/*
 * A pagetable entry of S390 has following format:
 *
 *  |   PFRA          |    |  OS  |
 * 0                   0IP0
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * I Page-Invalid Bit:    Page is not available for address-translation
 * P Page-Protection Bit: Store access not possible for page
 */

/*
 * A segmenttable entry of S390 has following format:
 *
 *  |   P-table origin      |  |PTL
 * 0                         IC
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * I Segment-Invalid Bit:    Segment is not available for address-translation
 * C Common-Segment Bit:     Segment is not private (PoP 3-30)
 * PTL Page-Table-Length:    Length of Page-table (PTL+1*16 entries -> up to 256 entries)
 */

/*
 * The segmenttable origin of S390 has following format:
 *
 *  |S-table origin   |     | STL |
 * X                   **GPS
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * X Space-Switch event:
 * G Segment-Invalid Bit:     *
 * P Private-Space Bit:       Segment is not private (PoP 3-30)
 * S Storage-Alteration:
 * STL Segment-Table-Length:  Length of Page-table (STL+1*16 entries -> up to 2048 entries)
 */

#define _PAGE_PRESENT   0x001          /* Software                         */
#define _PAGE_ACCESSED  0x002          /* Software accessed                */
#define _PAGE_DIRTY     0x004          /* Software dirty                   */
#define _PAGE_RO        0x200          /* HW read-only                     */
#define _PAGE_INVALID   0x400          /* HW invalid                       */

#define _PAGE_TABLE_LEN 0xf            /* only full page-tables            */
#define _PAGE_TABLE_COM 0x10           /* common page-table                */
#define _PAGE_TABLE_INV 0x20           /* invalid page-table               */
#define _SEG_PRESENT    0x001          /* Software (overlap with PTL)      */

#define _USER_SEG_TABLE_LEN    0x7f    /* user-segment-table up to 2 GB    */
#define _KERNEL_SEG_TABLE_LEN  0x7f    /* kernel-segment-table up to 2 GB  */

/*
 * User and Kernel pagetables are identical
 */

#define _PAGE_TABLE     (_PAGE_TABLE_LEN )
#define _KERNPG_TABLE   (_PAGE_TABLE_LEN )

/*
 * The Kernel segment-tables includes the User segment-table
 */

#define _SEGMENT_TABLE  (_USER_SEG_TABLE_LEN|0x80000000)
#define _KERNSEG_TABLE  (_KERNEL_SEG_TABLE_LEN)
/*
 * No mapping available
 */
#define PAGE_NONE       __pgprot(_PAGE_INVALID )

#define PAGE_SHARED     __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)
#define PAGE_COPY       __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_RO)
#define PAGE_READONLY   __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_RO)
#define PAGE_KERNEL     __pgprot(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_DIRTY)

/*
 * The S390 can't do page protection for execute, and considers that the same are read.
 * Also, write permissions imply read permissions. This is the closest we can get..
 */
#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_COPY
#define __P011  PAGE_COPY
#define __P100  PAGE_READONLY
#define __P101  PAGE_READONLY
#define __P110  PAGE_COPY
#define __P111  PAGE_COPY

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_SHARED
#define __S011  PAGE_SHARED
#define __S100  PAGE_READONLY
#define __S101  PAGE_READONLY
#define __S110  PAGE_SHARED
#define __S111  PAGE_SHARED

/*
 * Define this if things work differently on an i386 and an i486:
 * it will (on an i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 *
 * Kernel and User memory-access are done equal, so we don't need verify
 */
#undef TEST_VERIFY_AREA

/* page table for 0-4MB for everybody */

extern unsigned long pg0[1024];

/* zero page used for uninitialized stuff */
extern unsigned long empty_zero_page[1024];

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE(vaddr) ((unsigned long) empty_zero_page)

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR                    (8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK                        (~(sizeof(void*)-1))

/* sizeof(void*)==1<<SIZEOF_PTR_LOG2 */
/* 64-bit machines, beware!  SRB. */
#define SIZEOF_PTR_LOG2                 2

/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)



/* 
 * CR 7 (SPST) and cr 13 (HPST) are set to the user pgdir. 
 * Kernel is running in its own, disjunct address space,
 * running in primary address space.
 * Copy to/from user is done via access register mode with
 * access registers set to 0 or 1. For that purpose we need 
 * set up CR 7 with the user pgd.  
 * 
 */

#define SET_PAGE_DIR(tsk,pgdir)                                                 \
do {                                                                            \
        unsigned long __pgdir = (__pa(pgdir) & PAGE_MASK ) | _SEGMENT_TABLE; \
        (tsk)->tss.user_seg = __pgdir;                                        \
        if ((tsk) == current) {                                               \
                __asm__ __volatile__("lctl  7,7,%0": :"m" (__pgdir));         \
                __asm__ __volatile__("lctl  13,13,%0": :"m" (__pgdir));       \
        }                                                                     \
} while (0)


extern inline int pte_none(pte_t pte)           { return ((pte_val(pte) & (_PAGE_INVALID | _PAGE_RO)) ==  
							  _PAGE_INVALID); } 
extern inline int pte_present(pte_t pte)        { return pte_val(pte) & _PAGE_PRESENT; }
extern inline void pte_clear(pte_t *ptep)       { pte_val(*ptep) = _PAGE_INVALID; }

extern inline int pmd_none(pmd_t pmd)           { return pmd_val(pmd) & _PAGE_TABLE_INV; }
extern inline int pmd_bad(pmd_t pmd)            { return (pmd_val(pmd) == 0); }
extern inline int pmd_present(pmd_t pmd)        { return pmd_val(pmd) & _SEG_PRESENT; }
extern inline void pmd_clear(pmd_t * pmdp)      {
                                                        pmd_val(pmdp[0]) = _PAGE_TABLE_INV;
                                                        pmd_val(pmdp[1]) = _PAGE_TABLE_INV;
                                                        pmd_val(pmdp[2]) = _PAGE_TABLE_INV;
                                                        pmd_val(pmdp[3]) = _PAGE_TABLE_INV;
                                                }

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
extern inline int pgd_none(pgd_t pgd)           { return 0; }
extern inline int pgd_bad(pgd_t pgd)            { return 0; }
extern inline int pgd_present(pgd_t pgd)        { return 1; }
extern inline void pgd_clear(pgd_t * pgdp)      { }


/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern inline int pte_write(pte_t pte)          { return !(pte_val(pte) & _PAGE_RO); }
extern inline int pte_dirty(pte_t pte)          { return pte_val(pte) & _PAGE_DIRTY; }
extern inline int pte_young(pte_t pte)          { return pte_val(pte) & _PAGE_ACCESSED; }

/* who needs that
extern inline int pte_read(pte_t pte)           { return !(pte_val(pte) & _PAGE_INVALID); }
extern inline int pte_exec(pte_t pte)           { return !(pte_val(pte) & _PAGE_INVALID); }
extern inline pte_t pte_rdprotect(pte_t pte)    { pte_val(pte) |= _PAGE_INVALID; return pte; }
extern inline pte_t pte_exprotect(pte_t pte)    { pte_val(pte) |= _PAGE_INVALID; return pte; }
extern inline pte_t pte_mkread(pte_t pte)       { pte_val(pte) &= _PAGE_INVALID; return pte; }
extern inline pte_t pte_mkexec(pte_t pte)       { pte_val(pte) &= _PAGE_INVALID; return pte; }
*/

extern inline pte_t pte_wrprotect(pte_t pte)    { pte_val(pte) |= _PAGE_RO; return pte; }
extern inline pte_t pte_mkwrite(pte_t pte)      { pte_val(pte) &= ~_PAGE_RO ; return pte; }

extern inline pte_t pte_mkclean(pte_t pte)      { pte_val(pte) &= ~_PAGE_DIRTY; return pte; }
extern inline pte_t pte_mkdirty(pte_t pte)      { pte_val(pte) |= _PAGE_DIRTY; return pte; }

extern inline pte_t pte_mkold(pte_t pte)        { pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
extern inline pte_t pte_mkyoung(pte_t pte)      { pte_val(pte) |= _PAGE_ACCESSED; return pte; }


/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
#define mk_pte(page, pgprot) \
({ pte_t __pte; pte_val(__pte) = __pa(page) + pgprot_val(pgprot); __pte; })

/* This takes a physical page address that is used by the remapping functions */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; pte_val(__pte) = physpage + pgprot_val(pgprot); __pte; })

extern inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ pte_val(pte) = (pte_val(pte) & PAGE_MASK) | pgprot_val(newprot); return pte; }

#define pte_page(pte) \
((unsigned long) __va(pte_val(pte) & PAGE_MASK))

#define pmd_page(pmd) \
((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/* to find an entry in a page-table-directory */
#define pgd_offset(mm, address) \
((mm)->pgd + ((address) >> PGDIR_SHIFT))

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

/* Find an entry in the second-level page table.. */
extern inline pmd_t * pmd_offset(pgd_t * dir, unsigned long address)
{
        return (pmd_t *) dir;
}

/* Find an entry in the third-level page table.. */
#define pte_offset(pmd, address) \
((pte_t *) (pmd_page(*pmd) + ((address>>10) & ((PTRS_PER_PTE-1)<<2))))


static inline void __flush_tlb_one(struct mm_struct *mm,
                                   unsigned long addr)
{
	pgd_t * pgdir;
	pmd_t * pmd;
	pte_t * pte, *pto;
	
	pgdir = pgd_offset(mm, addr);
	if (pgd_none(*pgdir) || pgd_bad(*pgdir))
		return;
	pmd = pmd_offset(pgdir, addr);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return;
	pte = pte_offset(pmd,addr);

	/*
	 * S390 has 1mb segments, we are emulating 4MB segments
	 */

	pto = (pte_t*) (((unsigned long) pte) & 0x7ffffc00);
	       
       	__asm__ __volatile("    ic   0,2(%0)\n"
			   "    ipte %1,%2\n"
			   "    stc  0,2(%0)"
			   : : "a" (pte), "a" (pto), "a" (addr): "0");
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

#define pgd_quicklist (S390_lowcore.cpu_data.pgd_quick)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (S390_lowcore.cpu_data.pte_quick)
#define pgtable_cache_size (S390_lowcore.cpu_data.pgtable_cache_sz)

extern __inline__ pgd_t* get_pgd_slow(void)
{
        int i;
        pgd_t *pgd,*ret = (pgd_t *)__get_free_pages(GFP_KERNEL,2);
	if (ret)
		for (i=0,pgd=ret;i<USER_PTRS_PER_PGD;i++,pgd++)
			pmd_clear(pmd_offset(pgd,i*PGDIR_SIZE));
        return ret;
}

extern __inline__ pgd_t* get_pgd_fast(void)
{
        unsigned long *ret;
	
        if((ret = pgd_quicklist) != NULL) {
                pgd_quicklist = (unsigned long *)(*ret);
                ret[0] = ret[1];
                pgtable_cache_size--;
		/*
		 * Need to flush tlb, since private page tables
		 * are unique thru address of pgd and virtual address.
		 * If we reuse pgd we need to be sure no tlb entry
		 * with that pdg is left -> global flush
		 *
		 * Fixme: To avoid this global flush we should
		 * use pdg_quicklist as fix lenght fifo list
		 * and not as stack
		 */
        } else
                ret = (unsigned long *)get_pgd_slow();
        return (pgd_t *)ret;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
        *(unsigned long *)pgd = (unsigned long) pgd_quicklist;
        pgd_quicklist = (unsigned long *) pgd;
        pgtable_cache_size++;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
        free_pages((unsigned long)pgd,2);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t* get_pte_fast(void)
{
        unsigned long *ret;

        if((ret = (unsigned long *)pte_quicklist) != NULL) {
                pte_quicklist = (unsigned long *)(*ret);
                ret[0] = ret[1];
                pgtable_cache_size--;
        }
        return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
        *(unsigned long *)pte = (unsigned long) pte_quicklist;
        pte_quicklist = (unsigned long *) pte;
        pgtable_cache_size++;
}

extern __inline__ void free_pte_slow(pte_t *pte)
{
        free_page((unsigned long)pte);
}

/* We don't use pmd cache, so these are dummy routines */
extern __inline__ pmd_t *get_pmd_fast(void)
{
        return (pmd_t *)0;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
}

extern void __bad_pte(pmd_t *pmd);
extern void __bad_pte_kernel(pmd_t *pmd);

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)
#define pgd_free(pgd)           free_pgd_fast(pgd)
#define pgd_alloc()             get_pgd_fast()

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
        address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
        if (pmd_none(*pmd)) {
                pte_t * page = (pte_t *) get_pte_fast();

                if (!page)
                        return get_pte_kernel_slow(pmd, address);
                pmd_val(pmd[0]) = _KERNPG_TABLE + __pa(page);
                pmd_val(pmd[1]) = _KERNPG_TABLE + __pa(page+1024);
                pmd_val(pmd[2]) = _KERNPG_TABLE + __pa(page+2048);
                pmd_val(pmd[3]) = _KERNPG_TABLE + __pa(page+3072);
                return page + address;
        }
        if (pmd_bad(*pmd)) {
                __bad_pte_kernel(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
        address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

        if (pmd_none(*pmd))
                goto getnew;
        if (pmd_bad(*pmd))
                goto fix;
        return (pte_t *) pmd_page(*pmd) + address;
getnew:
{
        unsigned long page = (unsigned long) get_pte_fast();

        if (!page)
                return get_pte_slow(pmd, address);
        pmd_val(pmd[0]) = _PAGE_TABLE + __pa(page);
        pmd_val(pmd[1]) = _PAGE_TABLE + __pa(page+1024);
        pmd_val(pmd[2]) = _PAGE_TABLE + __pa(page+2048);
        pmd_val(pmd[3]) = _PAGE_TABLE + __pa(page+3072);
        return (pte_t *) page + address;
}
fix:
        __bad_pte(pmd);
        return NULL;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free(pmd_t * pmd)
{
}

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
        return (pmd_t *) pgd;
}

#define pmd_free_kernel         pmd_free
#define pmd_alloc_kernel        pmd_alloc

extern int do_check_pgt_cache(int, int);

#define set_pgdir(addr,entry) do { } while(0)

extern pgd_t swapper_pg_dir[] __attribute__ ((aligned (4096)));

/*
 * The S390 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
extern inline void update_mmu_cache(struct vm_area_struct * vma,
        unsigned long address, pte_t pte)
{
}

/*
 * a page-table entry has only 19 bit for offset and 7 bit for type
 * if bits 0, 20 or 23 are set, a translation specification exceptions occures, and it's
 * hard to find out the failing address
 * therefor, we zero out this bits
 */

#define SWP_TYPE(entry) (((entry) >> 1) & 0x3f)
#define SWP_OFFSET(entry) (((entry) >> 12) & 0x7FFFF )
#define SWP_ENTRY(type,offset) ((((type) << 1) | ((offset) << 12) |  \
				 _PAGE_INVALID | _PAGE_RO) & 0x7FFFF6FE)

#define module_map      vmalloc
#define module_unmap    vfree

#endif /* !__ASSEMBLY__ */

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
#define PageSkip(page)          (0)
#define kern_addr_valid(addr)   (1)

#endif /* _S390_PAGE_H */

