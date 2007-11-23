/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pagemap.h>

#include <asm/dma.h>
#include <asm/uaccess.h> /* for copy_to/from_user */
#include <asm/pgtable.h>

int nr_swap_pages = 0;
int nr_free_pages = 0;

/*
 * Free area management
 *
 * The free_area_list arrays point to the queue heads of the free areas
 * of different sizes
 */

#if CONFIG_AP1000
/* the AP+ needs to allocate 8MB contiguous, aligned chunks of ram
   for the ring buffers */
#define NR_MEM_LISTS 12
#else
#define NR_MEM_LISTS 10
#endif
#define NR_MEM_TYPES 2		/* GFP_DMA vs not for now. */

/* The start of this MUST match the start of "struct page" */
struct free_area_struct {
	struct page *next;
	struct page *prev;
	unsigned int * map;
	unsigned long count;
};

#define memory_head(x) ((struct page *)(x))

static struct free_area_struct free_area[NR_MEM_TYPES][NR_MEM_LISTS];

static inline void init_mem_queue(struct free_area_struct * head)
{
	head->next = memory_head(head);
	head->prev = memory_head(head);
}

static inline void add_mem_queue(struct free_area_struct * head, struct page * entry)
{
	struct page * next = head->next;

	entry->prev = memory_head(head);
	entry->next = next;
	next->prev = entry;
	head->next = entry;
	head->count++;
}

static inline void remove_mem_queue(struct page * entry)
{
	struct page * next = entry->next;
	struct page * prev = entry->prev;
	next->prev = prev;
	prev->next = next;
}

/*
 * Free_page() adds the page to the free lists. This is optimized for
 * fast normal cases (no error jumps taken normally).
 *
 * The way to optimize jumps for gcc-2.2.2 is to:
 *  - select the "normal" case and put it inside the if () { XXX }
 *  - no else-statements if you can avoid them
 *
 * With the above two rules, you get a straight-line execution path
 * for the normal case, giving better asm-code.
 */

/*
 * Buddy system. Hairy. You really aren't expected to understand this
 *
 * Hint: -mask = 1+~mask
 */
spinlock_t page_alloc_lock = SPIN_LOCK_UNLOCKED;

#define list(x) (mem_map+(x))
#define __free_pages_ok(map_nr, mask, area, index)		\
	nr_free_pages -= (mask);				\
	while ((mask) + (1 << (NR_MEM_LISTS-1))) {		\
		if (!test_and_change_bit((index), (area)->map))	\
			break;					\
		(area)->count--;				\
		remove_mem_queue(list((map_nr) ^ -(mask)));	\
		(mask) <<= 1;					\
		(area)++;					\
		(index) >>= 1;					\
		(map_nr) &= (mask);				\
	}							\
	add_mem_queue(area, list(map_nr));

static void free_local_pages(struct page * page) {
	unsigned long order = page->offset;
	unsigned int type = PageDMA(page) ? 1 : 0;
	struct free_area_struct *area;
	unsigned long map_nr = page - mem_map;
	unsigned long mask = (~0UL) << order;
	unsigned long index = map_nr >> (1 + order);

	area = free_area[type] + order;
	__free_pages_ok(map_nr, mask, area, index);
}

static inline void free_pages_ok(unsigned long map_nr, unsigned long order, unsigned type)
{
	struct free_area_struct *area;
	unsigned long index;
	unsigned long mask;
	unsigned long flags;
	struct page * page;

	if (current->flags & PF_FREE_PAGES)
		goto local_freelist;
 back_local_freelist:

	index = map_nr >> (1 + order);
	mask = (~0UL) << order;
	map_nr &= mask;

	spin_lock_irqsave(&page_alloc_lock, flags);
	area = free_area[type] + order;
	__free_pages_ok(map_nr, mask, area, index);
	spin_unlock_irqrestore(&page_alloc_lock, flags);
	return;

 local_freelist:
	/*
	 * This is a little subtle: if the allocation order
	 * wanted is major than zero we'd better take all the pages
	 * local since we must deal with fragmentation too and we
	 * can't rely on the nr_local_pages information.
	 */
	if ((current->nr_local_pages && !current->allocation_order) ||
	    in_interrupt())
		goto back_local_freelist;

	page = mem_map + map_nr;
	list_add((struct list_head *) page, &current->local_pages);
	page->offset = order;
	current->nr_local_pages++;
}

void __free_pages(struct page *page, unsigned long order)
{
	if (!PageReserved(page) && atomic_dec_and_test(&page->count)) {
		if (PageSwapCache(page))
			panic ("Freeing swap cache page");
		page->flags &= ~(1 << PG_referenced);
		free_pages_ok(page - mem_map, order, PageDMA(page) ? 1 : 0);
		return;
	}
}

void free_pages(unsigned long addr, unsigned long order)
{
	unsigned long map_nr = MAP_NR(addr);

	if (map_nr < max_mapnr)
		__free_pages(mem_map + map_nr, order);
}

/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define MARK_USED(index, order, area) \
	change_bit((index) >> (1+(order)), (area)->map)
#define ADDRESS(x) (PAGE_OFFSET + ((x) << PAGE_SHIFT))
#define RMQUEUE_TYPE(order, type) \
do { struct free_area_struct * area = free_area[type]+order; \
     unsigned long new_order = order; \
	do { struct page *prev = memory_head(area), *ret = prev->next; \
		if (memory_head(area) != ret) { \
			unsigned long map_nr; \
			(prev->next = ret->next)->prev = prev; \
			map_nr = ret - mem_map; \
			MARK_USED(map_nr, new_order, area); \
			nr_free_pages -= 1 << order; \
			area->count--; \
			EXPAND(ret, map_nr, order, new_order, area); \
			spin_unlock_irqrestore(&page_alloc_lock, flags); \
			return ADDRESS(map_nr); \
		} \
		new_order++; area++; \
	} while (new_order < NR_MEM_LISTS); \
} while (0)

#define EXPAND(map,index,low,high,area) \
do { unsigned long size = 1 << high; \
	while (high > low) { \
		area--; high--; size >>= 1; \
		add_mem_queue(area, map); \
		MARK_USED(index, high, area); \
		index += size; \
		map += size; \
	} \
	atomic_set(&map->count, 1); \
} while (0)

static void refile_local_pages(void)
{
	if (current->nr_local_pages) {
		struct page * page;
		struct list_head * entry;
		int nr_pages = current->nr_local_pages;

		while ((entry = current->local_pages.next) != &current->local_pages) {
			list_del(entry);
			page = (struct page *) entry;
			free_local_pages(page);
			if (!nr_pages--)
				panic("__get_free_pages local_pages list corrupted I");
		}
		if (nr_pages)
			panic("__get_free_pages local_pages list corrupted II");
		current->nr_local_pages = 0;
	}
}

unsigned long __get_free_pages(int gfp_mask, unsigned long order)
{
	unsigned long flags;

	if (order >= NR_MEM_LISTS)
		goto out;

#ifdef ATOMIC_MEMORY_DEBUGGING
	if ((gfp_mask & __GFP_WAIT) && in_interrupt()) {
		static int count = 0;
		if (++count < 5) {
			printk("gfp called nonatomically from interrupt %p\n",
				__builtin_return_address(0));
		}
		goto out;
	}
#endif

	/*
	 * Acquire lock before reading nr_free_pages to make sure it
	 * won't change from under us.
	 */
	spin_lock_irqsave(&page_alloc_lock, flags);

	/*
	 * If this is a recursive call, we'd better
	 * do our best to just allocate things without
	 * further thought.
	 */
	if (!(current->flags & PF_MEMALLOC)) {
		extern struct wait_queue * kswapd_wait;

		if (nr_free_pages > freepages.low)
			goto ok_to_allocate;

		if (waitqueue_active(&kswapd_wait))
			wake_up_interruptible(&kswapd_wait);

		/* Do we have to block or can we proceed? */
		if (nr_free_pages > freepages.min)
			goto ok_to_allocate;
		if (gfp_mask & __GFP_WAIT) {
			int freed;
			/*
			 * If the task is ok to sleep it's fine also
			 * if we release irq here.
			 */
			spin_unlock_irq(&page_alloc_lock);

			current->flags |= PF_MEMALLOC|PF_FREE_PAGES;
			current->allocation_order = order;
			freed = try_to_free_pages(gfp_mask);
			current->flags &= ~(PF_MEMALLOC|PF_FREE_PAGES);

			spin_lock_irq(&page_alloc_lock);
			refile_local_pages();

			/*
			 * Re-check we're still low on memory after we blocked
			 * for some time. Somebody may have released lots of
			 * memory from under us while we was trying to free
			 * the pages. We check against pages_high to be sure
			 * to succeed only if lots of memory is been released.
			 */
			if (nr_free_pages > freepages.high)
				goto ok_to_allocate;

			if (!freed && !(gfp_mask & (__GFP_MED | __GFP_HIGH)))
				goto nopage;
		}
	}
ok_to_allocate:
	/* if it's not a dma request, try non-dma first */
	if (!(gfp_mask & __GFP_DMA))
		RMQUEUE_TYPE(order, 0);
	RMQUEUE_TYPE(order, 1);
 nopage:
	spin_unlock_irqrestore(&page_alloc_lock, flags);
 out:
	return 0;
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas(void)
{
 	unsigned long order, flags;
	unsigned type;

	spin_lock_irqsave(&page_alloc_lock, flags);
	printk("Free pages:      %6dkB\n ( ",nr_free_pages<<(PAGE_SHIFT-10));
	printk("Free: %d (%d %d %d)\n",
		nr_free_pages,
		freepages.min,
		freepages.low,
		freepages.high);
	for (type = 0; type < NR_MEM_TYPES; type++) {
 		unsigned long total = 0;
		printk("%sDMA: ", type ? "" : "Non");
 		for (order=0 ; order < NR_MEM_LISTS; order++) {
			unsigned long nr = free_area[type][order].count;

			total += nr * ((PAGE_SIZE>>10) << order);
			printk("%lu*%lukB ", nr, (unsigned long)((PAGE_SIZE>>10) << order));
		}
		printk("= %lukB)\n", total);
	}
	spin_unlock_irqrestore(&page_alloc_lock, flags);
#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/*
 * set up the free-area data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
unsigned long __init free_area_init(unsigned long start_mem, unsigned long end_mem)
{
	mem_map_t * p;
	unsigned long i, j;

	/*
	 * Select nr of pages we try to keep free for important stuff
	 * with a minimum of 10 pages and a maximum of 256 pages, so
	 * that we don't waste too much memory on large systems.
	 * This is fairly arbitrary, but based on some behaviour
	 * analysis.
	 */
	i = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT+7);
	if (i < 10)
		i = 10;
	if (i > 256)
		i = 256;
	freepages.min = i;
	freepages.low = i * 2;
	freepages.high = i * 3;
	mem_map = (mem_map_t *) LONG_ALIGN(start_mem);
	p = mem_map + MAP_NR(end_mem);
	start_mem = LONG_ALIGN((unsigned long) p);
	memset(mem_map, 0, start_mem - (unsigned long) mem_map);
	do {
		--p;
		atomic_set(&p->count, 0);
		p->flags = (1 << PG_DMA) | (1 << PG_reserved);
	} while (p > mem_map);

	for (j = 0; j < NR_MEM_TYPES; j++) {
		unsigned long mask = PAGE_MASK;
		for (i = 0 ; i < NR_MEM_LISTS ; i++) {
			unsigned long bitmap_size;
			init_mem_queue(free_area[j]+i);
			mask += mask;
			end_mem = (end_mem + ~mask) & mask;
			bitmap_size = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT + i);
			bitmap_size = (bitmap_size + 7) >> 3;
			bitmap_size = LONG_ALIGN(bitmap_size);
			free_area[j][i].map = (unsigned int *) start_mem;
			memset((void *) start_mem, 0, bitmap_size);
			start_mem += bitmap_size;
		}
	}
	return start_mem;
}

/* 
 * Primitive swap readahead code. We simply read an aligned block of
 * (1 << page_cluster) entries in the swap area. This method is chosen
 * because it doesn't cost us any seek time.  We also make sure to queue
 * the 'original' request together with the readahead ones...  
 */
void swapin_readahead(unsigned long entry)
{
	int i;
	struct page *new_page;
	unsigned long offset = SWP_OFFSET(entry);
	struct swap_info_struct *swapdev = SWP_TYPE(entry) + swap_info;
	
	offset = (offset >> page_cluster) << page_cluster;

	i = 1 << page_cluster;
	do {
		/* Don't read-ahead past the end of the swap area */
		if (offset >= swapdev->max)
			break;
		/* Don't block on I/O for read-ahead */
		if (atomic_read(&nr_async_pages) >= pager_daemon.swap_cluster)
			break;
		/* Don't read in bad or busy pages */
		if (!swapdev->swap_map[offset])
			break;
		if (swapdev->swap_map[offset] == SWAP_MAP_BAD)
			break;
		if (test_bit(offset, swapdev->swap_lockmap))
			break;

		/* Ok, do the async read-ahead now */
		new_page = read_swap_cache_async(SWP_ENTRY(SWP_TYPE(entry), offset), 0);
		if (new_page != NULL)
			__free_page(new_page);
		offset++;
	} while (--i);
	return;
}

/*
 * The tests may look silly, but it essentially makes sure that
 * no other process did a swap-in on us just as we were waiting.
 *
 * Also, don't bother to add to the swap cache if this page-in
 * was due to a write access.
 */
int swap_in(struct task_struct * tsk, struct vm_area_struct * vma,
	pte_t * page_table, unsigned long entry, int write_access)
{
	unsigned long page;
	struct page *page_map = lookup_swap_cache(entry);

	if (!page_map) {
		swapin_readahead(entry);
		page_map = read_swap_cache(entry);
	}
	if (pte_val(*page_table) != entry) {
		if (page_map)
			free_page_and_swap_cache(page_address(page_map));
		return 1;
	}
	if (!page_map)
		return -1;

	page = page_address(page_map);
	vma->vm_mm->rss++;
	tsk->min_flt++;
	swap_free(entry);

	if (!write_access || is_page_shared(page_map)) {
		set_pte(page_table, mk_pte(page, vma->vm_page_prot));
		return 1;
	}

	/*
	 * The page is unshared and we're going to dirty it - so tear
	 * down the swap cache and give exclusive access to the page to
	 * this process.
	 */
	delete_from_swap_cache(page_map);
	set_pte(page_table, pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot))));
  	return 1;
}
