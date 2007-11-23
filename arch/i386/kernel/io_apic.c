/*
 *	Intel IO-APIC support for multi-pentium hosts.
 *
 *	(c) 1997 Ingo Molnar, Hajnalka Szabo
 *
 *	Many thanks to Stig Venaas for trying out countless experimental
 *	patches and reporting/debugging problems patiently!
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <asm/i82489.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/io.h>

#include "irq.h"

#define IO_APIC_BASE 0xfec00000

/*
 * volatile is justified in this case, it might change
 * spontaneously, GCC should not cache it
 */
volatile unsigned int * io_apic_reg = NULL;

/*
 * The structure of the IO-APIC:
 */
struct IO_APIC_reg_00 {
	__u32	__reserved_2	: 24,
		ID		:  4,
		__reserved_1	:  4;
} __attribute__ ((packed));

struct IO_APIC_reg_01 {
	__u32	version		:  8,
		__reserved_2	:  8,
		entries		:  8,
		__reserved_1	:  8;
} __attribute__ ((packed));

struct IO_APIC_reg_02 {
	__u32	__reserved_2	: 24,
		arbitration	:  4,
		__reserved_1	:  4;
} __attribute__ ((packed));

struct IO_APIC_route_entry {
	__u32	vector		:  8,
		delivery_mode	:  3,	/* 000: FIXED
					 * 001: lowest prio
					 * 111: ExtInt
					 */
		dest_mode	:  1,	/* 0: physical, 1: logical */
		delivery_status	:  1,
		polarity	:  1,
		irr		:  1,
		trigger		:  1,	/* 0: edge, 1: level */
		mask		:  1,	/* 0: enabled, 1: disabled */
		__reserved_2	: 15;

	union {		struct { __u32
					__reserved_1	: 24,
					physical_dest	:  4,
					__reserved_2	:  4;
			} physical;

			struct { __u32
					__reserved_1	: 24,
					logical_dest	:  8;
			} logical;
	} dest;

} __attribute__ ((packed));

#define UNEXPECTED_IO_APIC()						\
	{								\
		printk(" WARNING: unexpected IO-APIC, please mail\n");	\
		printk("          to linux-smp@vger.rutgers.edu\n");	\
	}

int nr_ioapic_registers = 0;			/* # of IRQ routing registers */
int mp_irq_entries = 0;				/* # of MP IRQ source entries */
struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];
						/* MP IRQ source entries */

unsigned int io_apic_read (unsigned int reg)
{
	*io_apic_reg = reg;
	return *(io_apic_reg+4);
}

void io_apic_write (unsigned int reg, unsigned int value)
{
	*io_apic_reg = reg;
	*(io_apic_reg+4) = value;
}

void enable_IO_APIC_irq (unsigned int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Enable it in the IO-APIC irq-routing table:
	 */
	*(((int *)&entry)+0) = io_apic_read(0x10+irq*2);
	entry.mask = 0;
	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
}

/*
 * this function is just here to make things complete, otherwise it's
 * unused
 */
void disable_IO_APIC_irq (unsigned int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	*(((int *)&entry)+0) = io_apic_read(0x10+irq*2);
	entry.mask = 1;
	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
}

void clear_IO_APIC_irq (unsigned int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	memset(&entry, 0, sizeof(entry));
	entry.mask = 1;
	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
	io_apic_write(0x11+2*irq, *(((int *)&entry)+1));
}

/*
 * support for broken MP BIOSes, enables hand-redirection of PIRQ0-3 to
 * specific CPU-side IRQs.
 */

#define MAX_PIRQS 8
int pirq_entries [MAX_PIRQS];
int pirqs_enabled;

void ioapic_pirq_setup(char *str, int *ints)
{
	int i, max;

	for (i=0; i<MAX_PIRQS; i++)
		pirq_entries[i]=-1;

	if (!ints) {
		pirqs_enabled=0;
		printk("PIRQ redirection SETUP, trusting MP-BIOS.\n");

	} else {
		pirqs_enabled=1;
		printk("PIRQ redirection SETUP, working around broken MP-BIOS.\n");
		max = MAX_PIRQS;
		if (ints[0] < MAX_PIRQS)
			max = ints[0];

		for (i=0; i < max; i++) {
			printk("... PIRQ%d -> IRQ %d\n", i, ints[i+1]);
			/*
			 * PIRQs are mapped upside down, usually.
			 */
			pirq_entries[MAX_PIRQS-i-1]=ints[i+1];
		}
	}
}

int find_irq_entry(int pin)
{
	int i;

	for (i=0; i<mp_irq_entries; i++)
		if ( (mp_irqs[i].mpc_irqtype == 0x00) &&
			(mp_irqs[i].mpc_dstirq == pin))

			return i;

	return -1;
}

void setup_IO_APIC_irqs (void)
{
	struct IO_APIC_route_entry entry;
	int i, idx, bus, irq, first_notcon=1;

	printk("init IO_APIC IRQs\n");

	for (i=0; i<nr_ioapic_registers; i++) {

		/*
		 * add it to the IO-APIC irq-routing table:
		 */
		memset(&entry,0,sizeof(entry));

		entry.delivery_mode = 1;		/* lowest prio */
		entry.dest_mode = 1;			/* logical delivery */
		entry.mask = 0;				/* enable IRQ */
		entry.dest.logical.logical_dest = 0xff;	/* all CPUs */

		idx = find_irq_entry(i);
		if (idx == -1) {
			if (first_notcon) {
				printk(" IO-APIC pin %d", i);
				first_notcon=0;
			} else
				printk(", %d", i);
			continue;
		}
		bus = mp_irqs[idx].mpc_srcbus;

		switch (mp_bus_id_to_type[bus])
		{
			case MP_BUS_ISA: /* ISA pin */
			{
				irq = mp_irqs[idx].mpc_srcbusirq;
				break;
			}
			case MP_BUS_PCI: /* PCI pin */
			{
				/*
				 * PCI IRQs are 'directly mapped'
				 */
				irq = i;
				break;
			}
			default:
			{
				printk("unknown bus type %d.\n",bus); 
				irq = 0;
				break;
			}
		}

		/*
		 * PCI IRQ redirection. Yes, limits are hardcoded.
		 */
		if ((i>=16) && (i<=19)) {
			if (pirq_entries[i-16] != -1) {
				if (!pirq_entries[i-16]) {
					printk("disabling PIRQ%d\n", i-16);
				} else {
					irq = pirq_entries[i-16];
					printk("using PIRQ%d -> IRQ %d\n",
							i-16, irq);
				}
			}
		}

		if (!IO_APIC_IRQ(irq))
			continue;

		entry.vector = IO_APIC_VECTOR(irq);

		/*
		 * Determine IRQ line polarity (high active or low active):
		 */
		switch (mp_irqs[idx].mpc_irqflag & 3)
		{
			case 0: /* conforms, ie. bus-type dependent polarity */
			{
				switch (mp_bus_id_to_type[bus])
				{
					case MP_BUS_ISA: /* ISA pin */
					{
						entry.polarity = 0;
						break;
					}
					case MP_BUS_PCI: /* PCI pin */
					{
						entry.polarity = 1;
						break;
					}
					default:
					{
						printk("broken BIOS!!\n");
						break;
					}
				}
				break;
			}
			case 1: /* high active */
			{
				entry.polarity = 0;
				break;
			}
			case 2: /* reserved */
			{
				printk("broken BIOS!!\n");
				break;
			}
			case 3: /* low active */
			{
				entry.polarity = 1;
				break;
			}
		}

		/*
		 * Determine IRQ trigger mode (edge or level sensitive):
		 */
		switch ((mp_irqs[idx].mpc_irqflag>>2) & 3)
		{
			case 0: /* conforms, ie. bus-type dependent */
			{
				switch (mp_bus_id_to_type[bus])
				{
					case MP_BUS_ISA: /* ISA pin, edge */
					{
						entry.trigger = 0;
						break;
					}
					case MP_BUS_PCI: /* PCI pin, level */
					{
						entry.trigger = 1;
						break;
					}
					default:
					{
						printk("broken BIOS!!\n");
						break;
					}
				}
				break;
			}
			case 1: /* edge */
			{
				entry.trigger = 0;
				break;
			}
			case 2: /* reserved */
			{
				printk("broken BIOS!!\n");
				break;
			}
			case 3: /* level */
			{
				entry.trigger = 1;
				break;
			}
		}

		io_apic_write(0x10+2*i, *(((int *)&entry)+0));
		io_apic_write(0x11+2*i, *(((int *)&entry)+1));
	}

	if (!first_notcon)
		printk(" not connected.\n");
}

void setup_IO_APIC_irq_ISA_default (unsigned int irq)
{
	struct IO_APIC_route_entry entry;

	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(&entry,0,sizeof(entry));

	entry.delivery_mode = 1;			/* lowest prio */
	entry.dest_mode = 1;				/* logical delivery */
	entry.mask = 1;					/* unmask IRQ now */
	entry.dest.logical.logical_dest = 0xff;		/* all CPUs */

	entry.vector = IO_APIC_VECTOR(irq);

	entry.polarity=0;
	entry.trigger=0;

	io_apic_write(0x10+2*irq, *(((int *)&entry)+0));
	io_apic_write(0x11+2*irq, *(((int *)&entry)+1));
}

int IO_APIC_get_PCI_irq_vector (int bus, int slot, int pci_pin)
{
	int i;

	for (i=0; i<mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mpc_srcbus;

		if (IO_APIC_IRQ(mp_irqs[i].mpc_dstirq) &&
		    (mp_bus_id_to_type[lbus] == MP_BUS_PCI) &&
		    !mp_irqs[i].mpc_irqtype &&
		    (bus == mp_bus_id_to_pci_bus[mp_irqs[i].mpc_srcbus]) &&
		    (slot == ((mp_irqs[i].mpc_srcbusirq >> 2) & 0x1f)) &&
		    (pci_pin == (mp_irqs[i].mpc_srcbusirq & 3)))

			return mp_irqs[i].mpc_dstirq;
	}
	return -1;
}

/*
 * There is a nasty bug in some older SMP boards, their mptable lies
 * about the timer IRQ. We do the following to work around the situation:
 *
 *	- timer IRQ defaults to IO-APIC IRQ
 *	- if this function detects that timer IRQs are defunct, then we fall
 *	  back to ISA timer IRQs
 */
static int timer_irq_works (void)
{
	unsigned int t1=jiffies;
	unsigned long flags;

	save_flags(flags);
	sti();

	udelay(100*1000);

	if (jiffies-t1>1)
		return 1;

	return 0;
}

void print_IO_APIC (void)
{
	int i;
	struct IO_APIC_reg_00 reg_00;
	struct IO_APIC_reg_01 reg_01;
	struct IO_APIC_reg_02 reg_02;

	*(int *)&reg_00 = io_apic_read(0);
	*(int *)&reg_01 = io_apic_read(1);
	*(int *)&reg_02 = io_apic_read(2);

	/*
	 * We are a bit conservative about what we expect, we have to
	 * know about every HW change ASAP ...
	 */
	printk("testing the IO APIC.......................\n");

	printk(".... register #00: %08X\n", *(int *)&reg_00);
	printk(".......    : physical APIC id: %02X\n", reg_00.ID);
	if (reg_00.__reserved_1 || reg_00.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(".... register #01: %08X\n", *(int *)&reg_01);
	printk(".......     : max redirection entries: %04X\n", reg_01.entries);
	if (	(reg_01.entries != 0x0f) && /* ISA-only Neptune boards */
		(reg_01.entries != 0x17)    /* ISA+PCI boards */
	)
		UNEXPECTED_IO_APIC();
	if (reg_01.entries == 0x0f)
		printk(".......       [IO-APIC cannot route PCI PIRQ 0-3]\n");

	printk(".......     : IO APIC version: %04X\n", reg_01.version);
	if (	(reg_01.version != 0x10) && /* oldest IO-APICs */
		(reg_01.version != 0x11)  /* my IO-APIC */
	)
		UNEXPECTED_IO_APIC();
	if (reg_01.__reserved_1 || reg_01.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(".... register #02: %08X\n", *(int *)&reg_02);
	printk(".......     : arbitration: %02X\n", reg_02.arbitration);
	if (reg_02.__reserved_1 || reg_02.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(".... IRQ redirection table:\n");

	printk(" NR  Log Phy ");
	printk("Mask Trig IRR Pol Stat Dest Deli Vect:   \n");

	for (i=0; i<=reg_01.entries; i++) {
		struct IO_APIC_route_entry entry;

		*(((int *)&entry)+0) = io_apic_read(0x10+i*2);
		*(((int *)&entry)+1) = io_apic_read(0x11+i*2);

		printk(" %02x  %03X  %02X   ",
			i,
			entry.dest.logical.logical_dest,
			entry.dest.physical.physical_dest
		);

		printk("%1d    %1d    %1d   %1d   %1d    %1d    %1d    %02X\n",
			entry.mask,
			entry.trigger,
			entry.irr,
			entry.polarity,
			entry.delivery_status,
			entry.dest_mode,
			entry.delivery_mode,
			entry.vector
		);
	}

	printk(".................................... done.\n");

	return;
}

static void init_sym_mode (void)
{
	printk("enabling Symmetric IO mode ... ");
		outb (0x70, 0x22);
		outb (0x01, 0x23);
	printk("...done.\n");
}

void init_pic_mode (void)
{
	printk("disabling Symmetric IO mode ... ");
		outb (0x70, 0x22);
		outb (0x00, 0x23);
	printk("...done.\n");
}

char ioapic_OEM_ID [16];
char ioapic_Product_ID [16];

struct ioapic_list_entry {
	char * oem_id;
	char * product_id;
};

struct ioapic_list_entry ioapic_whitelist [] = {

	{ "INTEL   "	, 	"PR440FX     "	},
	{ "INTEL   "	,	"82440FX     "	},
	{ "AIR     "	,	"KDI         "	},
	{ 0		,	0		}
};

struct ioapic_list_entry ioapic_blacklist [] = {

	{ "OEM00000"	,	"PROD00000000"	},
	{ 0		,	0		}
};


static int in_ioapic_list (struct ioapic_list_entry * table)
{
	for (;table->oem_id; table++)
		if ((!strcmp(table->oem_id,ioapic_OEM_ID)) &&
		    (!strcmp(table->product_id,ioapic_Product_ID)))
			return 1;
	return 0;
}

static int ioapic_whitelisted (void)
{
/*
 * Right now, whitelist everything to see whether the new parsing
 * routines really do work for everybody..
 */
#if 1
	return 1;
#else
	return in_ioapic_list(ioapic_whitelist);
#endif
}

static int ioapic_blacklisted (void)
{
	return in_ioapic_list(ioapic_blacklist);
}


void setup_IO_APIC (void)
{
	int i;
	/*
	 *      Map the IO APIC into kernel space
	 */

	printk("mapping IO APIC from standard address.\n");
	io_apic_reg = ioremap_nocache(IO_APIC_BASE,4096);
	printk("new virtual address: %p.\n",io_apic_reg);

	init_sym_mode();
	{
		struct IO_APIC_reg_01 reg_01;

		*(int *)&reg_01 = io_apic_read(1);
		nr_ioapic_registers = reg_01.entries+1;
	}

	/*
	 * do not trust the IO-APIC being empty at bootup
	 */
	for (i=0; i<nr_ioapic_registers; i++)
		clear_IO_APIC_irq (i);

#if DEBUG_1
	for (i=0; i<16; i++)
		if (IO_APIC_IRQ(i))
			setup_IO_APIC_irq_ISA_default (i);
#endif

	/*
	 * the following IO-APIC's can be enabled:
	 *
	 * - whitelisted ones
	 * - those which have no PCI pins connected
	 * - those for which the user has specified a pirq= parameter
	 */
	if (	ioapic_whitelisted() ||
		(nr_ioapic_registers == 16) ||
		pirqs_enabled)
	{
		printk("ENABLING IO-APIC IRQs\n");
		io_apic_irqs = ~((1<<2)|(1<<13));
	} else {
		if (ioapic_blacklisted())
			printk(" blacklisted board, DISABLING IO-APIC IRQs\n");
		else
			printk(" unlisted board, DISABLING IO-APIC IRQs\n");

		printk(" see Documentation/IO-APIC.txt to enable them\n");
		io_apic_irqs = 0;
	}

	init_IO_APIC_traps();
	setup_IO_APIC_irqs ();

	if (!timer_irq_works ()) {
		make_8259A_irq(0);
		if (!timer_irq_works ())
			panic("IO-APIC + timer doesnt work!");
		printk("..MP-BIOS bug: i8254 timer not connected to IO-APIC\n");
		printk("..falling back to 8259A-based timer interrupt\n");
	}

	printk("nr of MP irq sources: %d.\n", mp_irq_entries);
	printk("nr of IOAPIC registers: %d.\n", nr_ioapic_registers);
	print_IO_APIC();
}
