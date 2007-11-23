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

#define MODULE_DEVICE_TABLE(foo,bar)

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
int pci_compat_enable_device(struct pci_dev *dev);
void *compat_request_region (unsigned long start, unsigned long n, const char *name);
void * pci_compat_get_driver_data (struct pci_dev *dev);
void pci_compat_set_driver_data (struct pci_dev *dev, void *driver_data);

extern __inline__ void *
pci_alloc_consistent(struct pci_dev *hwdev,
                     size_t size, dma_addr_t *dma_handle) {
        void *ret;
        int gfp = GFP_ATOMIC;

        if (hwdev == NULL)
                gfp |= GFP_DMA;
        ret = (void *)__get_free_pages(gfp, get_order(size));

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
        free_pages((unsigned long)vaddr, get_order(size));
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

#endif
