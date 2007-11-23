#include "emu_wrapper.h"
#include <asm/page.h>
#include <asm/io.h>

static LIST_HEAD(pci_drivers);

struct pci_driver_mapping {
        struct pci_dev *dev;
        struct pci_driver *drv;
        void *driver_data;
};

#define PCI_MAX_MAPPINGS 16
static struct pci_driver_mapping drvmap [PCI_MAX_MAPPINGS] = { { NULL, } , };

void * pci_compat_get_driver_data (struct pci_dev *dev)
{
        int i;
        
        for (i = 0; i < PCI_MAX_MAPPINGS; i++)
                if (drvmap[i].dev == dev)
                        return drvmap[i].driver_data;
        
        return NULL;
}

void pci_compat_set_driver_data (struct pci_dev *dev, void *driver_data)
{
        int i;
        
        for (i = 0; i < PCI_MAX_MAPPINGS; i++)
                if (drvmap[i].dev == dev) {
                        drvmap[i].driver_data = driver_data;
                        return;
                } 
}

const struct pci_device_id *
pci_compat_match_device(const struct pci_device_id *ids, struct pci_dev *dev)
{
        u16 subsystem_vendor, subsystem_device;

        pci_read_config_word(dev, PCI_SUBSYSTEM_VENDOR_ID, &subsystem_vendor);
        pci_read_config_word(dev, PCI_SUBSYSTEM_ID, &subsystem_device);

        while (ids->vendor || ids->subvendor || ids->class_mask) {
                if ((ids->vendor == PCI_ANY_ID || ids->vendor == dev->vendor) &&
                    (ids->device == PCI_ANY_ID || ids->device == dev->device) &&
                    (ids->subvendor == PCI_ANY_ID || ids->subvendor == subsystem_vendor) &&
                    (ids->subdevice == PCI_ANY_ID || ids->subdevice == subsystem_device) &&
                    !((ids->class ^ dev->class) & ids->class_mask))
                        return ids;
                ids++;
        }
        return NULL;
}

static int
pci_announce_device(struct pci_driver *drv, struct pci_dev *dev)
{
        const struct pci_device_id *id;
	int found, i;

        if (drv->id_table) {
                id = pci_compat_match_device(drv->id_table, dev);
                if (!id)
                        return 0;
        } else
                id = NULL;

	found = 0;
	for (i = 0; i < PCI_MAX_MAPPINGS && !found; i++)
		if (!drvmap[i].dev) {
			drvmap[i].dev = dev;
			drvmap[i].drv = drv;
			found = 1;
		}

        if (drv->probe(dev, id) >= 0) { 
		if(found)
			return 1;
	} else {
               drvmap[i - 1].dev = NULL; 
        }
        return 0;
}

int
pci_compat_register_driver(struct pci_driver *drv)
{
        struct pci_dev *dev;
        int count = 0, found, i;
#ifdef CONFIG_PCI
        list_add_tail(&drv->node, &pci_drivers);
        pci_for_each_dev(dev) {
                found = 0;
                for (i = 0; i < PCI_MAX_MAPPINGS && !found; i++)
                        if (drvmap[i].dev == dev)
                                found = 1;
                if (!found)
                        count += pci_announce_device(drv, dev);
        }
#endif
        return count;
}

void
pci_compat_unregister_driver(struct pci_driver *drv)
{
        struct pci_dev *dev;
        int i, found;
#ifdef CONFIG_PCI
        list_del(&drv->node);
        pci_for_each_dev(dev) {
                found = 0;
                for (i = 0; i < PCI_MAX_MAPPINGS && !found; i++)
                        if (drvmap[i].dev == dev)
                                found = 1;
                if (found) {
                        if (drv->remove)
                                drv->remove(dev);
                        drvmap[i - 1].dev = NULL;
                }
        }
#endif
}

int pci_compat_enable_device(struct pci_dev *dev)
{
        return 0;
}

void *compat_request_region (unsigned long start, unsigned long n, const char *name)
{
        if (check_region (start, n) != 0)
                return NULL;
        request_region (start, n, name);
        return (void *) 1;
}

