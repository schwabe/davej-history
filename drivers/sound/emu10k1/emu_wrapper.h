#ifndef __EMU_WRAPPER_H
#define __EMU_WRAPPER_H

/* wrapper for 2.2 kernel */

#include <linux/wrapper.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <asm/page.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/tqueue.h>

#define vma_get_pgoff(v)	vma_get_offset(v)
#define wait_queue_head_t	struct wait_queue *
#define DECLARE_WAITQUEUE(a, b)	struct wait_queue a = {b, NULL};
#define init_waitqueue_head(a)	init_waitqueue(a)

#define init_MUTEX(a)		*(a) = MUTEX

#define UP_INODE_SEM(a)		up(a)
#define DOWN_INODE_SEM(a)	down(a)

#define GET_INODE_STRUCT()	struct inode *inode = file->f_dentry->d_inode

#define tasklet_hi_schedule(t)	queue_task((t), &tq_immediate); \
				mark_bh(IMMEDIATE_BH)

#define tasklet_init(t,f,d)	(t)->next = NULL; \
				(t)->sync = 0; \
				(t)->routine = (void (*)(void *))(f); \
				(t)->data = (void *)(d)

#define tasklet_struct		tq_struct 

#define tasklet_unlock_wait(t)	while (test_bit(0, &(t)->sync)) { }

#define __exit
#define __exitdata
#define __devinit
#define __devinitdata
#define __devexit
#define __devexitdata

/* Not sure what version aliases were introduced in, but certainly in 2.91.66.  */
#ifdef MODULE
  #if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 91)
    #define module_init(x)      int init_module(void) __attribute__((alias(#x)));
    #define module_exit(x)      void cleanup_module(void) __attribute__((alias(#x)));
  #else
    #define module_init(x)      int init_module(void) { return x(); }
    #define module_exit(x)      void cleanup_module(void) { x(); }
  #endif
#else
  #define module_init(x)
  #define module_exit(x)
#endif

#define MODULE_DEVICE_TABLE(foo,bar)

static __inline__ void list_add_tail(struct list_head *new, struct list_head *head)
{
        __list_add(new, head->prev, head);
}

#define list_for_each(pos, head) \
        for (pos = (head)->next; pos != (head); pos = pos->next)

#define pci_dma_supported(dev, mask) 1

#define PCI_ANY_ID (~0)

#define PCI_GET_DRIVER_DATA pci_compat_get_driver_data
#define PCI_SET_DRIVER_DATA pci_compat_set_driver_data

#define PCI_SET_DMA_MASK(dev,data)

#define pci_enable_device pci_compat_enable_device
#define pci_register_driver pci_compat_register_driver
#define pci_unregister_driver pci_compat_unregister_driver

#define pci_for_each_dev(dev) \
        for(dev = pci_devices; dev; dev = dev->next)

#define pci_resource_start(dev,bar) \
(((dev)->base_address[(bar)] & PCI_BASE_ADDRESS_SPACE) ? \
 ((dev)->base_address[(bar)] & PCI_BASE_ADDRESS_IO_MASK) : \
 ((dev)->base_address[(bar)] & PCI_BASE_ADDRESS_MEM_MASK))
#define pci_resource_len pci_compat_get_size

struct pci_device_id {
        unsigned int vendor, device;
        unsigned int subvendor, subdevice;
        unsigned int class, class_mask;
        unsigned long driver_data;
};

struct pci_driver {
        struct list_head node;
        struct pci_dev *dev;
        char *name;
        const struct pci_device_id *id_table;   /* NULL if wants all devices */
        int (*probe)(struct pci_dev *dev, const struct pci_device_id *id);      /* New device inserted */
        void (*remove)(struct pci_dev *dev);    /* Device removed (NULL if not a hot-plug capable driver) */
        void (*suspend)(struct pci_dev *dev);   /* Device suspended */
        void (*resume)(struct pci_dev *dev);    /* Device woken up */
};

const struct pci_device_id * pci_compat_match_device(const struct pci_device_id *ids, struct pci_dev *dev);
int pci_compat_register_driver(struct pci_driver *drv);
void pci_compat_unregister_driver(struct pci_driver *drv);
unsigned long pci_compat_get_size (struct pci_dev *dev, int n_base);
int pci_compat_enable_device(struct pci_dev *dev);
void *compat_request_region (unsigned long start, unsigned long n, const char *name);
void * pci_compat_get_driver_data (struct pci_dev *dev);
void pci_compat_set_driver_data (struct pci_dev *dev, void *driver_data);

typedef u32 dma_addr_t;

extern __inline__ int __compat_get_order(unsigned long size)
{
        int order;

        size = (size-1) >> (PAGE_SHIFT-1);
        order = -1;
        do {
                size >>= 1;
                order++;
        } while (size);
        return order;
}

extern __inline__ void *
pci_alloc_consistent(struct pci_dev *hwdev,
                     size_t size, dma_addr_t *dma_handle) {
        void *ret;
        int gfp = GFP_ATOMIC;

        if (hwdev == NULL)
                gfp |= GFP_DMA;
        ret = (void *)__get_free_pages(gfp, __compat_get_order(size));

        if (ret != NULL) {
                memset(ret, 0, size);
                *dma_handle = virt_to_bus(ret);
        }
        return ret;
}

extern __inline__ void
pci_free_consistent(struct pci_dev *hwdev, size_t size,
                    void *vaddr, dma_addr_t dma_handle)
{
        free_pages((unsigned long)vaddr, __compat_get_order(size));
}

static inline int pci_module_init(struct pci_driver *drv)
{
        int rc = pci_register_driver (drv);

        if (rc > 0)
                return 0;

        /* if we get here, we need to clean up pci driver instance
         * and return some sort of error */
        pci_unregister_driver (drv);
        
        return -ENODEV;
}

#define BUG() do { \
        printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
        __asm__ __volatile__(".byte 0x0f,0x0b"); \
} while (0)

#endif
