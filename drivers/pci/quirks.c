/*
 * $Id: quirks.c,v 1.5 1998/05/02 19:24:14 mj Exp $
 *
 * PCI Chipset-Specific Quirks
 *
 * Extracted from pci.c and rewritten by Martin Mares
 *
 * This is the right place for all special fixups for on-board
 * devices not depending on system architecture -- for example
 * bus bridges.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>

#undef DEBUG

#ifdef CONFIG_PCI_OPTIMIZE

/*
 *	The PCI Bridge Optimization -- Some BIOS'es are too lazy
 *	and are unable to turn on several features which can burst
 *	system performance.
 */

/*
 * An item of this structure has the following meaning:
 * for each optimization, the register address, the mask
 * and value to write to turn it on.
 */
struct optimization_type {
	const char	*type;
	const char	*off;
	const char	*on;
} bridge_optimization[] __initdata = {
	{"Cache L2",			"write through",	"write back"},
	{"CPU-PCI posted write",	"off",			"on"},
	{"CPU-Memory posted write",	"off",			"on"},
	{"PCI-Memory posted write",	"off",			"on"},
	{"PCI burst",			"off",			"on"}
};

#define NUM_OPTIMIZATIONS \
	(sizeof(bridge_optimization) / sizeof(bridge_optimization[0]))

struct bridge_mapping_type {
	unsigned char	addr;	/* config space address */
	unsigned char	mask;
	unsigned char	value;
} bridge_mapping[] = {
	/*
	 * Intel Neptune/Mercury/Saturn:
	 *	If the internal cache is write back,
	 *	the L2 cache must be write through!
	 *	I've to check out how to control that
	 *	for the moment, we won't touch the cache
	 */
	{0x0	,0x02	,0x02	},
	{0x53	,0x02	,0x02	},
	{0x53	,0x01	,0x01	},
	{0x54	,0x01	,0x01	},
	{0x54	,0x02	,0x02	},

	/*
	 * UMC 8891A Pentium chipset:
	 *	Why did you think UMC was cheaper ??
	 */
	{0x50	,0x10	,0x00	},
	{0x51	,0x40	,0x40	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
};

__initfunc(static void quirk_bridge(struct pci_dev *dev, int pos))
{
	struct bridge_mapping_type *bmap;
	unsigned char val;
	int i;

	pos *= NUM_OPTIMIZATIONS;
	for (i = 0; i < NUM_OPTIMIZATIONS; i++) {
		printk("    %s: ", bridge_optimization[i].type);
		bmap = &bridge_mapping[pos + i];
		if (!bmap->addr) {
			printk("Not supported.\n");
		} else {
			pci_read_config_byte(dev, bmap->addr, &val);
			if ((val & bmap->mask) == bmap->value)
				printk("%s.\n", bridge_optimization[i].on);
			else {
				printk("%s", bridge_optimization[i].off);
				pci_write_config_byte(dev,
						      bmap->addr,
						      (val & (0xff - bmap->mask)) + bmap->value);
				printk(" -> %s.\n", bridge_optimization[i].on);
			}
		}
	}
}

#endif


/* Deal with broken BIOS'es that neglect to enable passive release,
   which can cause problems in combination with the 82441FX/PPro MTRRs */
__initfunc(static void quirk_passive_release(struct pci_dev *dev, int arg))
{
	struct pci_dev *d = NULL;
	unsigned char dlc;

	/* We have to make sure a particular bit is set in the PIIX3
	   ISA bridge, so we have to go out and find it. */
	while ((d = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, d))) {
		pci_read_config_byte(d, 0x82, &dlc);
		if (!(dlc & 1<<1)) {
			printk("PIIX3: Enabling Passive Release\n");
			dlc |= 1<<1;
			pci_write_config_byte(d, 0x82, dlc);
		}
	}
}

/*  The VIA VP2/VP3/MVP3 seem to have some 'features'. There may be a workaround
    but VIA don't answer queries. If you happen to have good contacts at VIA
    ask them for me please -- Alan 
    
    This appears to be BIOS not version dependent. So presumably there is a 
    chipset level fix */
    

int isa_dma_bridge_buggy = 0;		/* Exported */
    
__initfunc(static void quirk_isa_dma_hangs(struct pci_dev *dev, int arg))
{
	if(!isa_dma_bridge_buggy)
	{
		isa_dma_bridge_buggy=1;
		printk(KERN_INFO "Activating ISA DMA hang workarounds.\n");
	}
}

/*
 *	VIA Apollo KT133 needs PCI latency patch
 *	Made according to a windows driver based patch by George E. Breese
 *	see PCI Latency Adjust on http://www.viahardware.com/download/viatweak.shtm
 *      Also see http://home.tiscalinet.de/au-ja/review-kt133a-1-en.html for
 *      the info on which Mr Breese based his work.
 *
 *	Updated based on further information from the site and also on
 *	information provided by VIA 
 */

__initfunc(static void quirk_vialatency(struct pci_dev *dev, int arg))
{
	struct pci_dev *p;
	u8 rev;
	u8 busarb;
	/* Ok we have a potential problem chipset here. Now see if we have
	   a buggy southbridge */
	   
	p=pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686, NULL);
	if(p!=NULL)
	{
		pci_read_config_byte(p, PCI_CLASS_REVISION, &rev);
		/* 0x40 - 0x4f == 686B, 0x10 - 0x2f == 686A; thanks Dan Hollis */
		/* Check for buggy part revisions */
		if (rev < 0x40 || rev > 0x42) 
			return;
	}
	else
	{
		p = pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8231, NULL);
		if(p==NULL)	/* No problem parts */
			return;
		pci_read_config_byte(p, PCI_CLASS_REVISION, &rev);
		/* Check for buggy part revisions */
		if (rev < 0x10 || rev > 0x12) 
			return;
	}
	
	/*
	 *	Ok we have the problem. Now set the PCI master grant to 
	 *	occur every master grant. The apparent bug is that under high
	 *	PCI load (quite common in Linux of course) you can get data
	 *	loss when the CPU is held off the bus for 3 bus master requests
	 *	This happens to include the IDE controllers....
	 *
	 *	VIA only apply this fix when an SB Live! is present but under
	 *	both Linux and Windows this isnt enough, and we have seen
	 *	corruption without SB Live! but with things like 3 UDMA IDE
	 *	controllers. So we ignore that bit of the VIA recommendation..
	 */

	pci_read_config_byte(dev, 0x76, &busarb);
	/* Set bit 4 and bi 5 of byte 76 to 0x01 
	   "Master priority rotation on every PCI master grant */
	busarb &= ~(1<<5);
	busarb |= (1<<4);
	pci_write_config_byte(dev, 0x76, busarb);
	printk(KERN_INFO "Applying VIA southbridge workaround.\n");
}

/*
 * Fix some problems with 'movntq' copies on Athlons. We need to ensure the
 * non functional memory stuff wasn't enabled by the BIOS (and thus fix
 * crashes when we use 100% memory bandwidth)
 *
 * VIA 8363 chipset:
 * VIA 8363,8622,8361 Northbridges:
 *  - bits  5, 6, 7 at offset 0x55 need to be turned off
 * VIA 8367 (KT266x) Northbridges:
 *  - bits  5, 6, 7 at offset 0x95 need to be turned off
 */

__initfunc(static void pci_fixup_via_athlon_bug(struct pci_dev *d, int arg))
{
 	u8 v;
	int where = 0x55;

	if (d->device == PCI_DEVICE_ID_VIA_8367_0) {
	        where = 0x95; /* the memory write queue timer register is 
                                different for the kt266x's: 0x95 not 0x55 */
	}
	pci_read_config_byte(d, where, &v);
	if (v & 0xe0) {
		printk("Trying to stomp on Athlon bug...\n");
		v &= 0x1f; /* clear bits 5, 6, 7 */
		pci_write_config_byte(d, where, v);
	}
}


typedef void (*quirk_handler)(struct pci_dev *, int);

/*
 * Mapping from quirk handler functions to names.
 */

struct quirk_name {
	quirk_handler handler;
	char *name;
};

static struct quirk_name quirk_names[] __initdata = {
#ifdef CONFIG_PCI_OPTIMIZE
	{ quirk_bridge,		"Bridge optimization" },
#endif
	{ quirk_passive_release,"Passive release enable" },
	{ quirk_isa_dma_hangs,	"Work around ISA DMA hangs" },
	{ pci_fixup_via_athlon_bug, "Athlon/VIA fixup" },
};


static inline char *get_quirk_name(quirk_handler handler)
{
	int i;

	for (i = 0; i < sizeof(quirk_names)/sizeof(quirk_names[0]); i++)
		if (handler == quirk_names[i].handler)
			return quirk_names[i].name;

	return NULL;
}
  

/*
 * Mapping from PCI vendor/device ID pairs to quirk function types and arguments
 */

struct quirk_info {
	unsigned short vendor, device;
	quirk_handler handler;
	unsigned short arg;
};

static struct quirk_info quirk_list[] __initdata = {
#ifdef CONFIG_PCI_OPTIMIZE
	{ PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_BRD,		quirk_bridge,	0x00 },
	{ PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8891A,	quirk_bridge,	0x01 },
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82424,	quirk_bridge,	0x00 },
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82434,	quirk_bridge,	0x00 },
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82430,	quirk_bridge,	0x00 },
#endif
	{ PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release,	0x00 },
	/*
	 * Its not totally clear which chipsets are the problematic ones
	 * This is the 82C586 variants.
	 */
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_0,	quirk_isa_dma_hangs,	0x00 },
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C596,	quirk_isa_dma_hangs,	0x00 },
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8363_0,	pci_fixup_via_athlon_bug, 0x00 },
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8622,	        pci_fixup_via_athlon_bug, 0x00 },
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8361,	        pci_fixup_via_athlon_bug, 0x00 },
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8367_0,	pci_fixup_via_athlon_bug, 0x00 },
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8363_0,	quirk_vialatency, 0x00 },
	{ PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8371_1,	quirk_vialatency, 0x00 },
	{ PCI_VENDOR_ID_VIA,	0x3112	/* Not out yet ? */,	quirk_vialatency, 0x00 },
};

__initfunc(void pci_quirks_init(void))
{
	struct pci_dev *d;
	int i;

#ifdef DEBUG
	printk("PCI: pci_quirks_init\n");
#endif
	for(d=pci_devices; d; d=d->next) {
		for(i=0; i<sizeof(quirk_list)/sizeof(quirk_list[0]); i++) {
			struct quirk_info *q = quirk_list + i;
			if (q->vendor == d->vendor && q->device == d->device) {
				printk("PCI: %02x:%02x [%04x/%04x]: %s (%02x)\n",
				       d->bus->number, d->devfn, d->vendor, d->device,
				       get_quirk_name(q->handler), q->arg);
				q->handler(d, q->arg);
			}
		}
	}
}
