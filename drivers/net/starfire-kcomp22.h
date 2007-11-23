#ifndef __STARFIRE_KCOMP22_H
#define __STARFIRE_KCOMP22_H

#include <linux/kcomp.h>
#include <asm/io.h>

/* MII constants */
#define MII_BMCR            0x00        /* Basic mode control register */
#define MII_BMSR            0x01        /* Basic mode status register  */
#define MII_ADVERTISE       0x04        /* Advertisement control reg   */
#define MII_LPA             0x05        /* Link partner ability reg    */

#define BMCR_FULLDPLX           0x0100  /* Full duplex                 */
#define BMCR_ANRESTART          0x0200  /* Auto negotiation restart    */
#define BMCR_ANENABLE           0x1000  /* Enable auto negotiation     */
#define BMCR_SPEED100           0x2000  /* Select 100Mbps              */
#define BMCR_RESET              0x8000  /* Reset the DP83840           */

#define BMSR_LSTATUS            0x0004  /* Link status                 */

#define ADVERTISE_10HALF        0x0020  /* Try for 10mbps half-duplex  */
#define ADVERTISE_10FULL        0x0040  /* Try for 10mbps full-duplex  */
#define ADVERTISE_100HALF       0x0080  /* Try for 100mbps half-duplex */
#define ADVERTISE_100FULL       0x0100  /* Try for 100mbps full-duplex */
#define ADVERTISE_100BASE4	0x0200	/* Try for 100mbps 4k packets  */

#define LPA_10HALF              0x0020  /* Can do 10mbps half-duplex   */
#define LPA_10FULL              0x0040  /* Can do 10mbps full-duplex   */
#define LPA_100HALF             0x0080  /* Can do 100mbps half-duplex  */
#define LPA_100FULL             0x0100  /* Can do 100mbps full-duplex  */

/* MII ioctls */
#define SIOCGMIIPHY	0x8947		/* Get address of MII PHY in use. */
#define SIOCGMIIREG	0x8948		/* Read MII PHY register.	*/
#define SIOCSMIIREG	0x8949		/* Write MII PHY register.	*/

/* This structure is used in all SIOCxMIIxxx ioctl calls */
struct mii_ioctl_data {
	u16		phy_id;
	u16		reg_num;
	u16		val_in;
	u16		val_out;
};

/* ethtool stuff */
#define SIOCETHTOOL	0x8946		/* Ethtool interface		*/

/* This should work for both 32 and 64 bit userland. */
struct ethtool_cmd {
	u32	cmd;
	u32	supported;	/* Features this interface supports */
	u32	advertising;	/* Features this interface advertises */
	u16	speed;		/* The forced speed, 10Mb, 100Mb, gigabit */
	u8	duplex;		/* Duplex, half or full */
	u8	port;		/* Which connector port */
	u8	phy_address;
	u8	transceiver;	/* Which tranceiver to use */
	u8	autoneg;	/* Enable or disable autonegotiation */
	u32	maxtxpkt;	/* Tx pkts before generating tx int */
	u32	maxrxpkt;	/* Rx pkts before generating rx int */
	u32	reserved[4];
};

/* these strings are set to whatever the driver author decides... */
struct ethtool_drvinfo {
	u32	cmd;
	char	driver[32];	/* driver short name, "tulip", "eepro100" */
	char	version[32];	/* driver version string */
	char	fw_version[32];	/* firmware version string, if applicable */
	char	bus_info[32];	/* Bus info for this interface.  For PCI
				 * devices, use pci_dev->slot_name. */
	char	reserved1[32];
	char	reserved2[32];
};

/* for passing single values */
struct ethtool_value {
	u32	cmd;
	u32	data;
};

/* CMDs currently supported */
#define ETHTOOL_GSET		0x00000001 /* Get settings. */
#define ETHTOOL_SSET		0x00000002 /* Set settings, privileged. */
#define ETHTOOL_GDRVINFO	0x00000003 /* Get driver info. */
#define ETHTOOL_GMSGLVL		0x00000007 /* Get driver message level */
#define ETHTOOL_SMSGLVL		0x00000008 /* Set driver msg level, priv. */
#define ETHTOOL_NWAY_RST	0x00000009 /* Restart autonegotiation, priv. */
#define ETHTOOL_GLINK		0x0000000a /* Get link status */

/* Indicates what features are supported by the interface. */
#define SUPPORTED_10baseT_Half		(1 << 0)
#define SUPPORTED_10baseT_Full		(1 << 1)
#define SUPPORTED_100baseT_Half		(1 << 2)
#define SUPPORTED_100baseT_Full		(1 << 3)
#define SUPPORTED_1000baseT_Half	(1 << 4)
#define SUPPORTED_1000baseT_Full	(1 << 5)
#define SUPPORTED_Autoneg		(1 << 6)
#define SUPPORTED_TP			(1 << 7)
#define SUPPORTED_AUI			(1 << 8)
#define SUPPORTED_MII			(1 << 9)
#define SUPPORTED_FIBRE			(1 << 10)

/* Indicates what features are advertised by the interface. */
#define ADVERTISED_10baseT_Half		(1 << 0)
#define ADVERTISED_10baseT_Full		(1 << 1)
#define ADVERTISED_100baseT_Half	(1 << 2)
#define ADVERTISED_100baseT_Full	(1 << 3)
#define ADVERTISED_1000baseT_Half	(1 << 4)
#define ADVERTISED_1000baseT_Full	(1 << 5)
#define ADVERTISED_Autoneg		(1 << 6)
#define ADVERTISED_TP			(1 << 7)
#define ADVERTISED_AUI			(1 << 8)
#define ADVERTISED_MII			(1 << 9)
#define ADVERTISED_FIBRE		(1 << 10)

/* The forced speed, 10Mb, 100Mb, gigabit. */
#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000

/* Duplex, half or full. */
#define DUPLEX_HALF		0x00
#define DUPLEX_FULL		0x01

/* Which connector port. */
#define PORT_TP			0x00
#define PORT_AUI		0x01
#define PORT_MII		0x02
#define PORT_FIBRE		0x03
#define PORT_BNC		0x04

/* Which tranceiver to use. */
#define XCVR_INTERNAL		0x00
#define XCVR_EXTERNAL		0x01

/* Enable or disable autonegotiation.  If this is set to enable,
 * the forced link modes above are completely ignored.
 */
#define AUTONEG_DISABLE		0x00
#define AUTONEG_ENABLE		0x01


static LIST_HEAD(pci_drivers);

struct pci_driver_mapping {
	struct pci_dev *dev;
	struct pci_driver *drv;
	void *driver_data;
};

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
	const struct pci_device_id *id_table;	/* NULL if wants all devices */
	int (*probe)(struct pci_dev *dev, const struct pci_device_id *id);		/* New device inserted */
	void (*remove)(struct pci_dev *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	void (*suspend)(struct pci_dev *dev);	/* Device suspended */
	void (*resume)(struct pci_dev *dev);	/* Device woken up */
};

#define PCI_MAX_MAPPINGS 16
static struct pci_driver_mapping drvmap [PCI_MAX_MAPPINGS] = { { NULL, } , };

#define __devinit			__init
#define __devinitdata			__initdata
#define __devexit
#define __devexit_p(foo)		foo
#define MODULE_DEVICE_TABLE(foo,bar)
#define SET_MODULE_OWNER(dev)
#define COMPAT_MOD_INC_USE_COUNT	MOD_INC_USE_COUNT
#define COMPAT_MOD_DEC_USE_COUNT	MOD_DEC_USE_COUNT
#define PCI_ANY_ID (~0)
#define IORESOURCE_MEM			2
#define PCI_DMA_FROMDEVICE		0
#define PCI_DMA_TODEVICE		0
#define PCI_SLOT_NAME(pci_dev)		""

#define pci_request_regions(pdev, name)	0
#define pci_release_regions(pdev)	do {} while(0)
#define del_timer_sync(timer)		del_timer(timer)
#define alloc_etherdev(size)		init_etherdev(NULL, size)
#define register_netdev(dev)		0

#ifdef CONFIG_SPARC64
typedef unsigned long dma_addr_t;
#endif /* CONFIG_SPARC64 */

static inline void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
					 dma_addr_t *dma_handle)
{
	void *virt_ptr;

	virt_ptr = kmalloc(size, GFP_KERNEL);
	*dma_handle = virt_to_bus(virt_ptr);
	return virt_ptr;
}
#define pci_free_consistent(cookie, size, ptr, dma_ptr)	kfree(ptr)
#define pci_map_single(cookie, address, size, dir)	virt_to_bus(address)
#define pci_unmap_single(cookie, address, size, dir)
#define pci_dma_sync_single(cookie, address, size, dir)
#undef	pci_resource_flags
#define pci_resource_flags(dev, i) \
  ((dev->base_address[i] & IORESOURCE_IO) ? IORESOURCE_IO : IORESOURCE_MEM)

static void * pci_get_drvdata (struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_MAX_MAPPINGS; i++)
		if (drvmap[i].dev == dev)
			return drvmap[i].driver_data;

	return NULL;
}

static void pci_set_drvdata (struct pci_dev *dev, void *driver_data)
{
	int i;

	for (i = 0; i < PCI_MAX_MAPPINGS; i++)
		if (drvmap[i].dev == dev) {
			drvmap[i].driver_data = driver_data;
			return;
		}
}

static const struct pci_device_id * __init
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

static int __init
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
	for (i = 0; i < PCI_MAX_MAPPINGS; i++)
		if (!drvmap[i].dev) {
			drvmap[i].dev = dev;
			drvmap[i].drv = drv;
			found = 1;
			break;
		}

	if (!found)
		return 0;

	if (drv->probe(dev, id) >= 0)
		return 1;

	/* clean up */
	drvmap[i].dev = NULL;
	return 0;
}

static int __init
pci_register_driver(struct pci_driver *drv)
{
	struct pci_dev *dev;
	int count = 0, found, i;
	list_add_tail(&drv->node, &pci_drivers);
	for (dev = pci_devices; dev; dev = dev->next) {
		found = 0;
		for (i = 0; i < PCI_MAX_MAPPINGS && !found; i++)
			if (drvmap[i].dev == dev)
				found = 1;
		if (!found)
			count += pci_announce_device(drv, dev);
	}
	return count;
}

static void
pci_unregister_driver(struct pci_driver *drv)
{
	struct pci_dev *dev;
	int i, found;
	list_del(&drv->node);
	for (dev = pci_devices; dev; dev = dev->next) {
		found = 0;
		for (i = 0; i < PCI_MAX_MAPPINGS; i++)
			if (drvmap[i].dev == dev) {
				found = 1;
				break;
			}
		if (found) {
			if (drv->remove)
				drv->remove(dev);
			drvmap[i].dev = NULL;
		}
	}
}

static inline int pci_module_init(struct pci_driver *drv)
{
	if (pci_register_driver(drv))
		return 0;
	return -ENODEV;
}

static struct pci_driver starfire_driver;

int __init starfire_probe(struct net_device *dev)
{
	static int __initdata probed = 0;

	if (probed)
		return -ENODEV;
	probed++;

	return pci_module_init(&starfire_driver);
}

#define init_tx_timer(dev, func, timeout)
#define kick_tx_timer(dev, func, timeout) \
	if (netif_queue_stopped(dev)) { \
		/* If this happens network layer tells us we're broken. */ \
		if (jiffies - dev->trans_start > timeout) \
			func(dev); \
	}

#define netif_start_if(dev)	dev->start = 1
#define netif_stop_if(dev)	dev->start = 0

#endif /* __STARFIRE_KCOMP22_H */
