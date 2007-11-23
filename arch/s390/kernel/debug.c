#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <asm/ebcdic.h>
#include <asm/debug.h>

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/ebcdic.h>

debug_info_t debug_areas[MAX_DEBUG_AREAS] = { {NULL, },};
debug_info_t *free_area = 0;
static int initialized = 0;

static spinlock_t debug_lock = SPIN_LOCK_UNLOCKED;

debug_info_t *
debug_register (char *name, int page_order, int nr_areas)
{
  debug_info_t *rc = 0;
  int i;
  long flags;

  if ( ! initialized ){
    debug_init();
    initialized = 1;
  }
  if (!free_area)
    {
      printk (KERN_WARNING "No free debug area\n");
      return NULL;
    }
  spin_lock_irqsave (&debug_lock, flags);
  rc = free_area;
  free_area = *((debug_info_t **) rc);

  memset(rc, 0, nr_areas * sizeof(debug_info_t));
  rc->areas = (debug_entry_t **) kmalloc (nr_areas *
					  sizeof (debug_entry_t *),
					  GFP_ATOMIC);
  if (!rc->areas)
    {
      goto noareas;
    }

  for (i = 0; i < nr_areas; i++)
    {
      rc->areas[i] = (debug_entry_t *) __get_free_pages (GFP_ATOMIC,
							 page_order);
      if (!rc->areas[i])
	{
	  for (i--; i >= 0; i--)
	    {
	      free_pages ((unsigned long) rc->areas[i], page_order);
	    }
	  goto nopages;
	}
    }
  rc->page_order = page_order;
  rc->nr_areas = nr_areas;
  rc->name = kmalloc (strlen (name) + 1, GFP_ATOMIC);
  strncpy (rc->name, name, strlen (name));
  ASCEBC(rc->name, strlen (name));
  rc->name[strlen (name)] = 0;

  rc->active_entry = kmalloc (nr_areas, GFP_ATOMIC);
  memset(rc->active_entry, 0, nr_areas * sizeof(int));
  rc->level=3;
  printk (KERN_INFO "reserved %d areas of %d pages for debugging %s\n",
	  nr_areas, 1 << page_order, name);
  goto exit;

nopages:
noareas:
  free_area = rc;
exit:
  spin_unlock_irqrestore (&debug_lock, flags);
  return rc;
}

void
debug_unregister (debug_info_t * id, char *name)
{
  int i = id->nr_areas;
  long flags;
  spin_lock_irqsave (&debug_lock, flags);
  printk (KERN_INFO "freeing debug area %p named '%s'\n", id, name);
  if (strncmp (name, id->name, strlen (name)))
    {
      printk (KERN_ERR "name '%s' does not match against '%s'\n",
	      name, id->name);
    }
  for (i--; i >= 0; i--)
    {
      free_pages ((unsigned long) id->areas[i], id->page_order);
    }
  kfree (id->areas);
  kfree (id->name);
  *((debug_info_t **) id) = free_area;
  free_area = id;
  spin_unlock_irqrestore (&debug_lock, flags);
  return;
}

static inline void
proceed_active_entry (debug_info_t * id)
{
  id->active_entry[id->active_area] =
    (id->active_entry[id->active_area]++) %
    ((PAGE_SIZE / sizeof (debug_entry_t)) << (id->page_order));
}

static inline void
proceed_active_area (debug_info_t * id)
{
  id->active_area = (id->active_area++) % id->nr_areas;
}

static inline debug_entry_t *
get_active_entry (debug_info_t * id)
{
  return &id->areas[id->active_area][id->active_entry[id->active_area]];
}

static inline debug_entry_t *
debug_common ( debug_info_t * id )
{
  debug_entry_t * active;
  proceed_active_entry (id);
  active = get_active_entry (id);
  STCK (active->id.stck);
  active->id.stck = active->id.stck >> 4;
  active->id.fields.cpuid = smp_processor_id ();
  active->caller = __builtin_return_address (0);
  return active;
}

void
debug_event (debug_info_t * id, int level, unsigned int tag)
{
  long flags;
  debug_entry_t *active;
  if (!id)
    {
      return;
    }
  if (level < id->level)
    {
      return;
    }
  spin_lock_irqsave (&id->lock, flags);
  active = debug_common(id);
  active->tag.tag = tag;
  spin_unlock_irqrestore (&id->lock, flags);
  return;
}

void
debug_text_event (debug_info_t * id, int level, char tag[4])
{
  long flags;
  debug_entry_t *active;
  if (!id)
    {
      return;
    }
  if (level < id->level)
    {
      return;
    }
  spin_lock_irqsave (&id->lock, flags);
  active = debug_common(id);
  strncpy ( active->tag.text, tag, 4);
  ASCEBC (active->tag.text, 4 );
  spin_unlock_irqrestore (&id->lock, flags);
  return;
}

void
debug_exception (debug_info_t * id, int level, unsigned int tag)
{
  long flags;
  debug_entry_t *active;
  if (!id)
    {
      return;
    }
  if (level < id->level)
    {
      return;
    }
  spin_lock_irqsave (&id->lock, flags);
  active = debug_common(id);
  active->tag.tag = tag;
  proceed_active_area (id);
  spin_unlock_irqrestore (&id->lock, flags);

  return;
}

void
debug_text_exception (debug_info_t * id, int level, char tag[4])
{
  long flags;
  debug_entry_t *active;
  if (!id)
    {
      return;
    }
  if (level < id->level)
    {
      return;
    }
  spin_lock_irqsave (&id->lock, flags);
  active = debug_common(id);
  strncpy ( active->tag.text, tag, 4);
  ASCEBC (active->tag.text, 4 );
  proceed_active_area (id);
  spin_unlock_irqrestore (&id->lock, flags);
  return;
}

int
debug_init (void)
{
  int rc = 0;
  int i;
  for (i = 0; i < MAX_DEBUG_AREAS - 1; i++)
    {
      *(debug_info_t **) (&debug_areas[i]) =
	(debug_info_t *) (&debug_areas[i + 1]);
    }
  *(debug_info_t **) (&debug_areas[i]) = (debug_info_t *) NULL;
  free_area = &(debug_areas[0]);
  printk (KERN_INFO "%d areas reserved for debugging information\n",
	  MAX_DEBUG_AREAS);
  return rc;
}

#ifdef MODULE
int
init_module (void)
{
  int rc = 0;
  rc = debug_init ();
  if (rc)
    {
      printk (KERN_INFO "An error occurred with debug_init\n");
    }

  {				/* test section */
    debug_info_t *a[4];
    printk (KERN_INFO "registering 1, %p\n", a[0] =
	    debug_register ("debug1", 1, 1));
    printk (KERN_INFO "registering 2, %p\n", a[1] =
	    debug_register ("debug2", 1, 2));
    printk (KERN_INFO "registering 3, %p\n", a[2] =
	    debug_register ("debug3", 2, 1));
    printk (KERN_INFO "registering 4, %p\n", a[3] =
	    debug_register ("debug4", 2, 2));
    debug_unregister (a[0], "debug1");
    debug_unregister (a[1], "debug3");
    printk (KERN_INFO "registering 1, %p\n", a[0] =
	    debug_register ("debug5", 1, 1));
    printk (KERN_INFO "registering 2, %p\n", a[1] =
	    debug_register ("debug6", 1, 2));
    debug_unregister (a[2], "debug2");
    debug_unregister (a[3], "debug4");
    debug_unregister (a[0], "debug5");
    debug_unregister (a[1], "debug6");
  }
  return rc;
}

void
cleanup_module (void)
{

  return;
}

#endif /* MODULE */
