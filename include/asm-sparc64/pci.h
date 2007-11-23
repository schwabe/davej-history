#ifndef _SPARC64_PCI_H
#define _SPARC64_PCI_H

#include <linux/fs.h>
#include <linux/mm.h>

/* Return the index of the PCI controller for device PDEV. */

extern int pci_controller_num(struct pci_dev *pdev);

/* Platform support for /proc/bus/pci/X/Y mmap()s. */

#define HAVE_PCI_MMAP

extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state,
			       int write_combine);

#endif /* !(_SPARC64_PCI_H) */
