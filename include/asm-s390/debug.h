#ifndef DEBUG_H
#define DEBUG_H

#include <asm/spinlock.h>

#ifdef __KERNEL__

#define MAX_DEBUG_AREAS 16

#define STCK(x) asm volatile ("STCK %0":"=m" (x))

typedef struct
{
  union
  {
    struct
    {
      unsigned long long cpuid:4;
      unsigned long long clock:60;
    }
    fields;
    unsigned long long stck;
  }
  id;
  void *caller;
  union
  {
    unsigned long tag;
    char text[4];
  }
  tag;
}
debug_entry_t;

typedef struct
{
  char *name;
  int level;
  int nr_areas;
  int page_order;
  debug_entry_t **areas;
  int active_area;
  int *active_entry;
  spinlock_t lock;
}
debug_info_t;

int debug_init (void);
debug_info_t *debug_register (char *name, int pages_index, int nr_areas);
void debug_unregister (debug_info_t * id, char *name);
void debug_event (debug_info_t * id, int level, unsigned int tag);
void debug_text_event (debug_info_t * id, int level, char tag[4]);
void debug_exception (debug_info_t * id, int level, unsigned int tag);
void debug_text_exception (debug_info_t * id, int level, char tag[4]);

/*
   define the debug levels:
   - 0 No debugging output to console or syslog
   - 1 Log internal errors to syslog, ignore check conditions 
   - 2 Log internal errors and check conditions to syslog
   - 3 Log internal errors to console, log check conditions to syslog
   - 4 Log internal errors and check conditions to console
   - 5 panic on internal errors, log check conditions to console
   - 6 panic on both, internal errors and check conditions
 */

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 4
#endif

#define INTERNAL_ERRMSG(x,y...) "E" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_WRNMSG(x,y...) "W" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_INFMSG(x,y...) "I" __FILE__ "%d: " x, __LINE__, y
#define INTERNAL_DEBMSG(x,y...) "D" __FILE__ "%d: " x, __LINE__, y

#if DEBUG_LEVEL > 0
#define PRINT_DEBUG(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_INFO(x...) printk ( KERN_INFO PRINTK_HEADER x )
#define PRINT_WARN(x...) printk ( KERN_WARNING PRINTK_HEADER x )
#define PRINT_ERR(x...) printk ( KERN_ERR PRINTK_HEADER x )
#define PRINT_FATAL(x...) panic ( PRINTK_HEADER x )
#else
#define PRINT_DEBUG(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_INFO(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_WARN(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_ERR(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_FATAL(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#endif				/* DASD_DEBUG */

#if DASD_DEBUG > 4
#define INTERNAL_ERROR(x...) PRINT_FATAL ( INTERNAL_ERRMSG ( x ) )
#elif DASD_DEBUG > 2
#define INTERNAL_ERROR(x...) PRINT_ERR ( INTERNAL_ERRMSG ( x ) )
#elif DASD_DEBUG > 0
#define INTERNAL_ERROR(x...) PRINT_WARN ( INTERNAL_ERRMSG ( x ) )
#else
#define INTERNAL_ERROR(x...)
#endif				/* DASD_DEBUG */

#if DASD_DEBUG > 5
#define INTERNAL_CHECK(x...) PRINT_FATAL ( INTERNAL_CHKMSG ( x ) )
#elif DASD_DEBUG > 3
#define INTERNAL_CHECK(x...) PRINT_ERR ( INTERNAL_CHKMSG ( x ) )
#elif DASD_DEBUG > 1
#define INTERNAL_CHECK(x...) PRINT_WARN ( INTERNAL_CHKMSG ( x ) )
#else
#define INTERNAL_CHECK(x...)
#endif				/* DASD_DEBUG */

#undef DEBUG_MALLOC
#ifdef DEBUG_MALLOC
void *b;
#define kmalloc(x...) (PRINT_INFO(" kmalloc %p\n",b=kmalloc(x)),b)
#define kfree(x) PRINT_INFO(" kfree %p\n",x);kfree(x)
#define get_free_page(x...) (PRINT_INFO(" gfp %p\n",b=get_free_page(x)),b)
#define __get_free_pages(x...) (PRINT_INFO(" gfps %p\n",b=__get_free_pages(x)),b)
#endif				/* DEBUG_MALLOC */

#endif				/* __KERNEL__ */
#endif				/* DEBUG_H */
