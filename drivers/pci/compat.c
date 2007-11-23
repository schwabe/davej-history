/*
 *	$Id: compat.c,v 1.1 1998/02/16 10:35:50 mj Exp $
 *
 *	PCI Bus Services -- Function For Backward Compatibility
 *
 *	Copyright 1998 Martin Mares
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>

int
pcibios_find_class(unsigned int class, unsigned short index, unsigned char *bus, unsigned char *devfn)
{
	struct pci_dev *dev = NULL;
	int cnt = 0;

	while ((dev = pci_find_class(class, dev)))
		if (index == cnt++) {
			*bus = dev->bus->number;
			*devfn = dev->devfn;
			return PCIBIOS_SUCCESSFUL;
		}
	return PCIBIOS_DEVICE_NOT_FOUND;
}


int
pcibios_find_device(unsigned short vendor, unsigned short device, unsigned short index,
		    unsigned char *bus, unsigned char *devfn)
{
	struct pci_dev *dev = NULL;
	int cnt = 0;

	while ((dev = pci_find_device(vendor, device, dev)))
		if (index == cnt++) {
			*bus = dev->bus->number;
			*devfn = dev->devfn;
			return PCIBIOS_SUCCESSFUL;
		}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

/* 2.4 compatibility */

unsigned long pci_resource_len (struct pci_dev *dev, int n_base)
{
        u32 l, sz;
        int reg = PCI_BASE_ADDRESS_0 + (n_base << 2);

        /* XXX temporarily disable I/O and memory decoding for this device? */

        pci_read_config_dword (dev, reg, &l);
        if (l == 0xffffffff)
                return 0;

        pci_write_config_dword (dev, reg, ~0);
        pci_read_config_dword (dev, reg, &sz);
        pci_write_config_dword (dev, reg, l);

        if (!sz || sz == 0xffffffff)
                return 0;
        if ((l & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY) {
                sz = ~(sz & PCI_BASE_ADDRESS_MEM_MASK);
        } else {
                sz = ~(sz & PCI_BASE_ADDRESS_IO_MASK) & 0xffff;
        }

        return sz + 1;
}
