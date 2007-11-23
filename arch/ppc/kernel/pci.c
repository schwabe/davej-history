/*
 * $Id: pci.c,v 1.54.2.1 1999/07/20 05:04:41 paulus Exp $
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/openpic.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/pci.h>
#include <asm/residual.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/gg2.h>
#include <asm/uaccess.h>

#include "pci.h"

unsigned long isa_io_base     = 0;
unsigned long isa_mem_base    = 0;
unsigned long pci_dram_offset = 0;

int pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned char *val)
{
	return ppc_md.pcibios_read_config_byte(bus,dev_fn,offset,val);
}
int pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short *val)
{
	return ppc_md.pcibios_read_config_word(bus,dev_fn,offset,val);
}
int pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned int *val)
{
	return ppc_md.pcibios_read_config_dword(bus,dev_fn,offset,val);
}
int pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned char val)
{
	return ppc_md.pcibios_write_config_byte(bus,dev_fn,offset,val);
}
int pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned short val)
{
	return ppc_md.pcibios_write_config_word(bus,dev_fn,offset,val);
}
int pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			       unsigned char offset, unsigned int val)
{
	return ppc_md.pcibios_write_config_dword(bus,dev_fn,offset,val);
}

int pcibios_present(void)
{
	return 1;
}

void __init pcibios_init(void)
{
}


void __init pcibios_fixup(void)
{
	ppc_md.pcibios_fixup();
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
}

char __init *pcibios_setup(char *str)
{
	return str;
}

#ifndef CONFIG_MBX
/* Recursively searches any node that is of type PCI-PCI bridge. Without
 * this, the old code would miss children of P2P bridges and hence not
 * fix IRQ's for cards located behind P2P bridges.
 * - Ranjit Deshpande, 01/20/99
 */
void __init fix_intr(struct device_node *node, struct pci_dev *dev)
{
	unsigned int *reg, *class_code;

	for (; node != 0;node = node->sibling) {
		class_code = (unsigned int *) get_property(node, "class-code", 0);
		if(class_code && (*class_code >> 8) == PCI_CLASS_BRIDGE_PCI)
			fix_intr(node->child, dev);
		reg = (unsigned int *) get_property(node, "reg", 0);
		if (reg == 0 || ((reg[0] >> 8) & 0xff) != dev->devfn)
			continue;
		/* this is the node, see if it has interrupts */
		if (node->n_intrs > 0) 
			dev->irq = node->intrs[0].line;
		break;
	}
}
#endif


void *
pci_dev_io_base(unsigned char bus, unsigned char devfn, int physical)
{
	if (!ppc_md.pci_dev_io_base) {
		/* Please, someone fix this for non-pmac machines, we
		 * need either the virtual or physical PCI IO base
		 */
		return 0;
	}
	return ppc_md.pci_dev_io_base(bus, devfn, physical);
}

void *
pci_dev_mem_base(unsigned char bus, unsigned char devfn)
{
	/* Default memory base is 0 (1:1 mapping) */
	if (!ppc_md.pci_dev_mem_base) {
		/* Please, someone fix this for non-pmac machines.*/
		return 0;
	}
	return ppc_md.pci_dev_mem_base(bus, devfn);
}

/* Returns the root-bridge number (Uni-N number) of a device */
int
pci_dev_root_bridge(unsigned char bus, unsigned char devfn)
{
	/* Defaults to 0 */
	if (!ppc_md.pci_dev_root_bridge)
		return 0;
	return ppc_md.pci_dev_root_bridge(bus, devfn);
}

/*
 * Those syscalls are derived from the Alpha versions, they
 * allow userland apps to retreive the per-device iobase and
 * mem-base. They also provide wrapper for userland to do
 * config space accesses.
 * The "host_number" returns the number of the Uni-N sub bridge
 */

asmlinkage int
sys_pciconfig_read(unsigned long bus, unsigned long dfn,
		   unsigned long off, unsigned long len,
		   unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!pcibios_present())
		return -ENOSYS;
	
	switch (len) {
	case 1:
		err = pcibios_read_config_byte(bus, dfn, off, &ubyte);
		put_user(ubyte, buf);
		break;
	case 2:
		err = pcibios_read_config_word(bus, dfn, off, &ushort);
		put_user(ushort, (unsigned short *)buf);
		break;
	case 4:
		err = pcibios_read_config_dword(bus, dfn, off, &uint);
		put_user(uint, (unsigned int *)buf);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

asmlinkage int
sys_pciconfig_write(unsigned long bus, unsigned long dfn,
		    unsigned long off, unsigned long len,
		    unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!pcibios_present())
		return -ENOSYS;

	switch (len) {
	case 1:
		err = get_user(ubyte, buf);
		if (err)
			break;
		err = pcibios_write_config_byte(bus, dfn, off, ubyte);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	case 2:
		err = get_user(ushort, (unsigned short *)buf);
		if (err)
			break;
		err = pcibios_write_config_word(bus, dfn, off, ushort);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	case 4:
		err = get_user(uint, (unsigned int *)buf);
		if (err)
			break;
		err = pcibios_write_config_dword(bus, dfn, off, uint);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

/* Provide information on locations of various I/O regions in physical
 * memory.  Do this on a per-card basis so that we choose the right
 * root bridge.
 * Note that the returned IO or memory base is a physical address
 */

asmlinkage long
sys_pciconfig_iobase(long which, unsigned long bus, unsigned long devfn)
{
	long result = -EOPNOTSUPP;
	
	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)pci_dev_root_bridge(bus, devfn);
	case IOBASE_MEMORY:
		return (long)pci_dev_mem_base(bus, devfn);
	case IOBASE_IO:
		result = (long)pci_dev_io_base(bus, devfn, 1);
		if (result == 0)
			result = -EOPNOTSUPP;
		break;
	}

	return result;
}
