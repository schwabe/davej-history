/* Compatibility for various Linux kernel versions */

#ifndef _LINUX_ISDN_COMPAT_H
#define _LINUX_ISDN_COMPAT_H

#ifdef __KERNEL__


#include <linux/pci.h>
#ifdef __powerpc__
static inline int pci_enable_device(struct pci_dev *dev)
{
	u16 cmd;
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_SERR;
	cmd &= ~PCI_COMMAND_FAST_BACK;
	pci_write_config_word(dev, PCI_COMMAND, cmd);
	return(0);
}
#else
static inline int pci_enable_device(struct pci_dev *dev)
{
	return 0;
}
#endif /* __powerpc__ */

#define PCI_ANY_ID (~0)

/* as this is included multiple times, we make it inline */

static inline struct pci_dev * pci_find_subsys(unsigned int vendor, unsigned int device,
					unsigned int ss_vendor, unsigned int ss_device,
					struct pci_dev *from)
{
	unsigned short subsystem_vendor, subsystem_device;

	while ((from = pci_find_device(vendor, device, from))) {
		pci_read_config_word(from, PCI_SUBSYSTEM_VENDOR_ID, &subsystem_vendor);
		pci_read_config_word(from, PCI_SUBSYSTEM_ID, &subsystem_device);
		if ((ss_vendor == PCI_ANY_ID || subsystem_vendor == ss_vendor) &&
		    (ss_device == PCI_ANY_ID || subsystem_device == ss_device))
			return from;
	}
	return NULL;
}

#endif /* __KERNEL__ */
#endif /* _LINUX_ISDN_COMPAT_H */
