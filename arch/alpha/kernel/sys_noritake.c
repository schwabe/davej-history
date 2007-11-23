/*
 *	linux/arch/alpha/kernel/sys_noritake.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998 Richard Henderson
 *
 * Code supporting the NORITAKE (AlphaServer 1000A), 
 * CORELLE (AlphaServer 800), and ALCOR Primo (AlphaStation 600A).
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_apecs.h>
#include <asm/core_cia.h>

#include "proto.h"
#include "irq.h"
#include "bios32.h"
#include "machvec.h"


static void 
noritake_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq <= 15)
		if (irq <= 7)
			outb(mask, 0x21);	/* ISA PIC1 */
		else
			outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else if (irq <= 31)
		outw(~(mask >> 16), 0x54a);
	else
		outw(~(mask >> 32), 0x54c);
}

static void 
noritake_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary registers of NORITAKE */
	pld = ((unsigned long) inw(0x54c) << 32) |
		((unsigned long) inw(0x54a) << 16) |
		((unsigned long) inb(0xa0)  <<  8) |
		((unsigned long) inb(0x20));

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i < 16) {
			isa_device_interrupt(vector, regs);
		} else {
			handle_irq(i, i, regs);
		}
	}
}

static void 
noritake_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;

	ack = irq = (vector - 0x800) >> 4;

	/*
	 * I really hate to do this, too, but the NORITAKE SRM console also
	 *  reports PCI vectors *lower* than I expected from the bit numbers
	 *  in the documentation.
	 * But I really don't want to change the fixup code for allocation
	 *  of IRQs, nor the alpha_irq_mask maintenance stuff, both of which
	 *  look nice and clean now.
	 * So, here's this additional grotty hack... :-(
	 */
	if (irq >= 16)
		ack = irq = irq + 1;

	handle_irq(irq, ack, regs);
}

static void __init
noritake_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	if (alpha_using_srm)
		alpha_mv.device_interrupt = noritake_srm_device_interrupt;

	outw(~(alpha_irq_mask >> 16), 0x54a); /* note invert */
	outw(~(alpha_irq_mask >> 32), 0x54c); /* note invert */
	enable_irq(2);			/* enable cascade */
}


/*
 * PCI Fixup configuration.
 *
 * Summary @ 0x542, summary register #1:
 * Bit      Meaning
 * 0        All valid ints from summary regs 2 & 3
 * 1        QLOGIC ISP1020A SCSI
 * 2        Interrupt Line A from slot 0
 * 3        Interrupt Line B from slot 0
 * 4        Interrupt Line A from slot 1
 * 5        Interrupt line B from slot 1
 * 6        Interrupt Line A from slot 2
 * 7        Interrupt Line B from slot 2
 * 8        Interrupt Line A from slot 3
 * 9        Interrupt Line B from slot 3
 *10        Interrupt Line A from slot 4
 *11        Interrupt Line B from slot 4
 *12        Interrupt Line A from slot 5
 *13        Interrupt Line B from slot 5
 *14        Interrupt Line A from slot 6
 *15        Interrupt Line B from slot 6
 *
 * Summary @ 0x544, summary register #2:
 * Bit      Meaning
 * 0        OR of all unmasked ints in SR #2
 * 1        OR of secondary bus ints
 * 2        Interrupt Line C from slot 0
 * 3        Interrupt Line D from slot 0
 * 4        Interrupt Line C from slot 1
 * 5        Interrupt line D from slot 1
 * 6        Interrupt Line C from slot 2
 * 7        Interrupt Line D from slot 2
 * 8        Interrupt Line C from slot 3
 * 9        Interrupt Line D from slot 3
 *10        Interrupt Line C from slot 4
 *11        Interrupt Line D from slot 4
 *12        Interrupt Line C from slot 5
 *13        Interrupt Line D from slot 5
 *14        Interrupt Line C from slot 6
 *15        Interrupt Line D from slot 6
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  7       Intel PCI-EISA bridge chip
 *  8       DEC PCI-PCI bridge chip
 * 11       PCI on board slot 0
 * 12       PCI on board slot 1
 * 13       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

static int __init
noritake_map_irq(struct pci_dev *dev, int slot, int pin)
{
	static char irq_tab[15][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		/* note: IDSELs 16, 17, and 25 are CORELLE only */
		{ 16+1,  16+1,  16+1,  16+1,  16+1},  /* IdSel 16,  QLOGIC */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 17, S3 Trio64 */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 18,  PCEB */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 19,  PPB  */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 20,  ???? */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 21,  ???? */
		{ 16+2,  16+2,  16+3,  32+2,  32+3},  /* IdSel 22,  slot 0 */
		{ 16+4,  16+4,  16+5,  32+4,  32+5},  /* IdSel 23,  slot 1 */
		{ 16+6,  16+6,  16+7,  32+6,  32+7},  /* IdSel 24,  slot 2 */
		{ 16+8,  16+8,  16+9,  32+8,  32+9},	/* IdSel 25,  slot 3 */
		/* The following 5 are actually on PCI bus 1, which is 
		   across the built-in bridge of the NORITAKE only.  */
		{ 16+1,  16+1,  16+1,  16+1,  16+1},  /* IdSel 16,  QLOGIC */
		{ 16+8,  16+8,  16+9,  32+8,  32+9},  /* IdSel 17,  slot 3 */
		{16+10, 16+10, 16+11, 32+10, 32+11},  /* IdSel 18,  slot 4 */
		{16+12, 16+12, 16+13, 32+12, 32+13},  /* IdSel 19,  slot 5 */
		{16+14, 16+14, 16+15, 32+14, 32+15},  /* IdSel 20,  slot 6 */
	};
	const long min_idsel = 5, max_idsel = 19, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static int __init
noritake_swizzle(struct pci_dev *dev, int *pinp)
{
	int slot, pin = *pinp;

	/* Check first for the built-in bridge */
	if (PCI_SLOT(dev->bus->self->devfn) == 8) {
		slot = PCI_SLOT(dev->devfn) + 15; /* WAG! */
	}
	else
	{
		/* Must be a card-based bridge.  */
		do {
			if (PCI_SLOT(dev->bus->self->devfn) == 8) {
				slot = PCI_SLOT(dev->devfn) + 15;
				break;
			}
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn)) ;

			/* Move up the chain of bridges.  */
			dev = dev->bus->self;
			/* Slot of the next bridge.  */
			slot = PCI_SLOT(dev->devfn);
		} while (dev->bus->self);
	}
	*pinp = pin;
	return slot;
}

static void __init
noritake_pci_fixup(void)
{
	layout_all_busses(EISA_DEFAULT_IO_BASE,APECS_AND_LCA_DEFAULT_MEM_BASE);
	common_pci_fixup(noritake_map_irq, noritake_swizzle);
}

static void __init
noritake_primo_pci_fixup(void)
{
	layout_all_busses(EISA_DEFAULT_IO_BASE, DEFAULT_MEM_BASE);
	common_pci_fixup(noritake_map_irq, noritake_swizzle);
}

#if defined(CONFIG_ALPHA_GENERIC) || !defined(CONFIG_ALPHA_PRIMO)
static void
noritake_apecs_machine_check(unsigned long vector, unsigned long la_ptr,
		     struct pt_regs * regs)
{
#define MCHK_NO_DEVSEL 0x205U
#define MCHK_NO_TABT 0x204U

	struct el_common *mchk_header;
	struct el_apecs_procdata *mchk_procdata;
	struct el_apecs_mikasa_sysdata_mcheck *mchk_sysdata;
	unsigned long *ptr;
	unsigned int code; /* workaround EGCS problem */
	int i;

	mchk_header = (struct el_common *)la_ptr;

	mchk_procdata = (struct el_apecs_procdata *)
		(la_ptr + mchk_header->proc_offset
		 - sizeof(mchk_procdata->paltemp));

	mchk_sysdata = (struct el_apecs_mikasa_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);

#ifdef DEBUG
	printk("noritake_apecs_machine_check: vector=0x%lx la_ptr=0x%lx\n",
	       vector, la_ptr);
	printk("        pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
	       regs->pc, mchk_header->size, mchk_header->proc_offset,
	       mchk_header->sys_offset);
	printk("noritake_apecs_machine_check: expected %d DCSR 0x%lx"
	       " PEAR 0x%lx\n", apecs_mcheck_expected,
	       mchk_sysdata->epic_dcsr, mchk_sysdata->epic_pear);
	ptr = (unsigned long *)la_ptr;
	for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
		printk(" +%lx %lx %lx\n", i*sizeof(long), ptr[i], ptr[i+1]);
	}
#endif

	/*
	 * Check if machine check is due to a badaddr() and if so,
	 * ignore the machine check.
	 */

	code = mchk_header->code; /* workaround EGCS problem */

	if (apecs_mcheck_expected &&
	    (code == MCHK_NO_DEVSEL || code == MCHK_NO_TABT))
	{
		apecs_mcheck_expected = 0;
		apecs_mcheck_taken = 1;
		mb();
		mb(); /* magic */
		apecs_pci_clr_err();
		wrmces(0x7);
		mb();
		draina();
	}
	else if (vector == 0x620 || vector == 0x630) {
		/* Disable correctable from now on.  */
		wrmces(0x1f);
		mb();
		draina();
		printk("noritake_apecs_machine_check: HW correctable (0x%lx)\n",
		       vector);
	}
	else {
		printk(KERN_CRIT "NORITAKE APECS machine check:\n");
		printk(KERN_CRIT "  vector=0x%lx la_ptr=0x%lx code=0x%x\n",
		       vector, la_ptr, code);
		printk(KERN_CRIT
		       "  pc=0x%lx size=0x%x procoffset=0x%x sysoffset 0x%x\n",
		       regs->pc, mchk_header->size, mchk_header->proc_offset,
		       mchk_header->sys_offset);
		printk(KERN_CRIT "  expected %d DCSR 0x%lx PEAR 0x%lx\n",
		       apecs_mcheck_expected, mchk_sysdata->epic_dcsr,
		       mchk_sysdata->epic_pear);

		ptr = (unsigned long *)la_ptr;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(KERN_CRIT " +%lx %lx %lx\n",
			       i*sizeof(long), ptr[i], ptr[i+1]);
		}
#if 0
		/* doesn't work with MILO */
		show_regs(regs);
#endif
	}
}
#endif


/*
 * The System Vectors
 */

#if defined(CONFIG_ALPHA_GENERIC) || !defined(CONFIG_ALPHA_PRIMO)
struct alpha_machine_vector noritake_mv __initmv = {
	vector_name:		"Noritake",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	DO_APECS_BUS,
	machine_check:		noritake_apecs_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		48,
	irq_probe_mask:		_PROBE_MASK(48),
	update_irq_hw:		noritake_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	noritake_device_interrupt,

	init_arch:		apecs_init_arch,
	init_irq:		noritake_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		noritake_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(noritake)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_PRIMO)
struct alpha_machine_vector noritake_primo_mv __initmv = {
	vector_name:		"Noritake-Primo",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_CIA_IO,
	DO_CIA_BUS,
	machine_check:		cia_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,

	nr_irqs:		48,
	irq_probe_mask:		_PROBE_MASK(48),
	update_irq_hw:		noritake_update_irq_hw,
	ack_irq:		generic_ack_irq,
	device_interrupt:	noritake_device_interrupt,

	init_arch:		cia_init_arch,
	init_irq:		noritake_init_irq,
	init_pit:		generic_init_pit,
	pci_fixup:		noritake_primo_pci_fixup,
	kill_arch:		generic_kill_arch,
};
ALIAS_MV(noritake_primo)
#endif
