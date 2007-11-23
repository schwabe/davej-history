
#ifndef DEBUG_H
#define DEBUG_H

#include <asm/spinlock.h>

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

#endif
