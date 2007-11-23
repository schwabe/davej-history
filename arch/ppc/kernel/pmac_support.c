/*
 * Miscellaneous procedures for dealing with the PowerMac hardware.
 * Contains support for the nvram.
 * 
 *   Copyright (C) 2000 Paul Mackerras
 *   Copyright (C) 2000 Benjamin Herrenschmidt
 *
 * ToDo: Handle type 0 and type 1 "NameRegistry" NVRAM properties. Those
 *       contain, among other things, the brightness, video mode, etc...
 */
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/reboot.h>
#include <linux/nvram.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/init.h>
#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/prom.h>
#include <asm/adb.h>
#include <asm/pmu.h>
#include <asm/machdep.h>
#include <asm/nvram.h>
#include <asm/backlight.h>

#undef DEBUG

#define NVRAM_SIZE		0x2000	/* 8kB of non-volatile RAM */

#define CORE99_SIGNATURE	0x5a
#define CORE99_ADLER_START	0x14

/* Core99 nvram is a flash */
#define CORE99_FLASH_STATUS_DONE	0x80
#define CORE99_FLASH_STATUS_ERR		0x38
#define CORE99_FLASH_CMD_ERASE_CONFIRM	0xd0
#define CORE99_FLASH_CMD_ERASE_SETUP	0x20
#define CORE99_FLASH_CMD_RESET		0xff
#define CORE99_FLASH_CMD_WRITE_SETUP	0x40

/* CHRP NVRAM header */
struct chrp_header {
	u8	signature;
	u8	cksum;
	u16	len;
	char    name[12];
	u8	data[0];
};

struct core99_header {
	struct chrp_header hdr;
	u32	adler;
	u32	generation;
	u32	reserved[2];
};

/*
 * Read and write the non-volatile RAM on PowerMacs and CHRP machines.
 */
static int nvram_naddrs;
static volatile unsigned char *nvram_addr;
static volatile unsigned char *nvram_data;
static int nvram_mult, is_core_99;
static int core99_bank = 0;
static int nvram_partitions[3];

static struct backlight_controller *backlighter = NULL;
static void* backlighter_data = NULL;
static int backlight_autosave = 0;
static int backlight_level = BACKLIGHT_MAX;
static int backlight_enabled = 1;

/* FIXME: kmalloc fails to allocate the image now that I had to move it
 *        before time_init(). For now, I allocate a static buffer here
 *        but it's a waste of space on all but core99 machines
 */
#if 0
static char* nvram_image;
#else
static char nvram_image[NVRAM_SIZE];
#endif

extern int pmac_newworld;

static u8 __pmac
chrp_checksum(struct chrp_header* hdr)
{
	u8 *ptr;
	u16 sum = hdr->signature;
	for (ptr = (u8 *)&hdr->len; ptr < hdr->data; ptr++)
		sum += *ptr;
	while (sum > 0xFF)
		sum = (sum & 0xFF) + (sum>>8);
	return sum;
}

static u32 __pmac
core99_calc_adler(u8 *buffer)
{
	int cnt;
	u32 low, high;

   	buffer += CORE99_ADLER_START;
	low = 1;
	high = 0;
	for (cnt=0; cnt<(NVRAM_SIZE-CORE99_ADLER_START); cnt++) {
		if ((cnt % 5000) == 0) {
			high  %= 65521UL;
			high %= 65521UL;
		}
		low += buffer[cnt];
		high += low;
	}
	low  %= 65521UL;
	high %= 65521UL;
  
	return (high << 16) | low;
}

static u32 __pmac
core99_check(u8* datas)
{
	struct core99_header* hdr99 = (struct core99_header*)datas;

	if (hdr99->hdr.signature != CORE99_SIGNATURE) {
#ifdef DEBUG
		printk("Invalid signature\n");
#endif		
		return 0;
	}
	if (hdr99->hdr.cksum != chrp_checksum(&hdr99->hdr)) {
#ifdef DEBUG
		printk("Invalid checksum\n");
#endif
		return 0;
	}
	if (hdr99->adler != core99_calc_adler(datas)) {
#ifdef DEBUG
		printk("Invalid adler\n");
#endif
		return 0;
	}
	return hdr99->generation;
}

static int __pmac
core99_erase_bank(int bank)
{
	int stat, i;
	
	u8* base = (u8 *)nvram_data + core99_bank*NVRAM_SIZE;
	
	out_8(base, CORE99_FLASH_CMD_ERASE_SETUP);
	out_8(base, CORE99_FLASH_CMD_ERASE_CONFIRM);
	do { stat = in_8(base); }
	while(!(stat & CORE99_FLASH_STATUS_DONE));
	out_8(base, CORE99_FLASH_CMD_RESET);
	if (stat & CORE99_FLASH_STATUS_ERR) {
		printk("nvram: flash error 0x%02x on erase !\n", stat);
		return -ENXIO;
	}
	for (i=0; i<NVRAM_SIZE; i++)
		if (base[i] != 0xff) {
			printk("nvram: flash erase failed !\n");
			return -ENXIO;
		}
	return 0;
}

static int __pmac
core99_write_bank(int bank, u8* datas)
{
	int i, stat = 0;
	
	u8* base = (u8 *)nvram_data + core99_bank*NVRAM_SIZE;
	
	for (i=0; i<NVRAM_SIZE; i++) {
		out_8(base+i, CORE99_FLASH_CMD_WRITE_SETUP);
		out_8(base+i, datas[i]);
		do { stat = in_8(base); }
		while(!(stat & CORE99_FLASH_STATUS_DONE));
		if (stat & CORE99_FLASH_STATUS_ERR)
			break;
	}
	out_8(base, CORE99_FLASH_CMD_RESET);
	if (stat & CORE99_FLASH_STATUS_ERR) {
		printk("nvram: flash error 0x%02x on write !\n", stat);
		return -ENXIO;
	}
	for (i=0; i<NVRAM_SIZE; i++)
		if (base[i] != datas[i]) {
			printk("nvram: flash write failed !\n");
			return -ENXIO;
		}
	return 0;	
}

static void __pmac
lookup_partitions(void)
{
	u8 buffer[17];
	int i, offset;
	struct chrp_header* hdr;

	if (pmac_newworld) {
		nvram_partitions[pmac_nvram_OF] = -1;
		nvram_partitions[pmac_nvram_XPRAM] = -1;
		nvram_partitions[pmac_nvram_NR] = -1;
		hdr = (struct chrp_header *)buffer;
	
		offset = 0;
		buffer[16] = 0;
		do {
			for (i=0;i<16;i++)
				buffer[i] = nvram_read_byte(offset+i);
			if (!strcmp(hdr->name, "common"))
				nvram_partitions[pmac_nvram_OF] = offset + 0x10;
			if (!strcmp(hdr->name, "APL,MacOS75")) {
				nvram_partitions[pmac_nvram_XPRAM] = offset + 0x10;
				nvram_partitions[pmac_nvram_NR] = offset + 0x110;
			}
			offset += (hdr->len * 0x10);
		} while(offset < NVRAM_SIZE);
	} else {
		nvram_partitions[pmac_nvram_OF] = 0x1800;
		nvram_partitions[pmac_nvram_XPRAM] = 0x1300;
		nvram_partitions[pmac_nvram_NR] = 0x1400;
	}	
#ifdef DEBUG
	printk("nvram: OF partition at 0x%x\n", nvram_partitions[pmac_nvram_OF]);
	printk("nvram: XP partition at 0x%x\n", nvram_partitions[pmac_nvram_XPRAM]);
	printk("nvram: NR partition at 0x%x\n", nvram_partitions[pmac_nvram_NR]);
#endif	
}

__init
void pmac_nvram_init(void)
{
	struct device_node *dp;

	nvram_naddrs = 0;

	dp = find_devices("nvram");
	if (dp == NULL) {
		printk(KERN_ERR "Can't find NVRAM device\n");
		return;
	}
	nvram_naddrs = dp->n_addrs;
	is_core_99 = device_is_compatible(dp, "nvram,flash");
	printk("pmac nvram is core99: %d\n", is_core_99);
	if (is_core_99) {
		int i;
		u32 gen_bank0, gen_bank1;
		
		if (nvram_naddrs < 1) {
			printk(KERN_ERR "nvram: no address\n");
			return;
		}
#if 0
		nvram_image = kmalloc(NVRAM_SIZE, GFP_KERNEL);
		if (!nvram_image) {
			printk(KERN_ERR "nvram: can't allocate image\n");
			return;
		}
#endif		
		nvram_data = ioremap(dp->addrs[0].address, NVRAM_SIZE*2);
#ifdef DEBUG
		printk("nvram: Checking bank 0...\n");
#endif
		gen_bank0 = core99_check((u8 *)nvram_data);
		gen_bank1 = core99_check((u8 *)nvram_data + NVRAM_SIZE);
		core99_bank = (gen_bank0 < gen_bank1) ? 1 : 0;
#ifdef DEBUG
		printk("nvram: gen0=%d, gen1=%d\n", gen_bank0, gen_bank1);
		printk("nvram: Active bank is: %d\n", core99_bank);
#endif
		for (i=0; i<NVRAM_SIZE; i++)
			nvram_image[i] = nvram_data[i + core99_bank*NVRAM_SIZE];
	} else if (_machine == _MACH_chrp && nvram_naddrs == 1) {
		nvram_data = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_mult = 1;
	} else if (nvram_naddrs == 1) {
		nvram_data = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_mult = (dp->addrs[0].size + NVRAM_SIZE - 1) / NVRAM_SIZE;
	} else if (nvram_naddrs == 2) {
		nvram_addr = ioremap(dp->addrs[0].address, dp->addrs[0].size);
		nvram_data = ioremap(dp->addrs[1].address, dp->addrs[1].size);
	} else if (nvram_naddrs == 0 && adb_controller &&
		adb_controller->kind == ADB_VIAPMU) {
		nvram_naddrs = -1;
	} else {
		printk(KERN_ERR "Don't know how to access NVRAM with %d addresses\n",
		       nvram_naddrs);
	}
	lookup_partitions();
}

void __pmac
pmac_nvram_update(void)
{
	struct core99_header* hdr99;
	
	if (!is_core_99 || !nvram_data || !nvram_image)
		return;
	if (!memcmp(nvram_image, (u8*)nvram_data + core99_bank*NVRAM_SIZE,
		NVRAM_SIZE))
		return;
#ifdef DEBUG
	printk("Updating nvram...\n");
#endif
	hdr99 = (struct core99_header*)nvram_image;
	hdr99->generation++;
	hdr99->hdr.signature = CORE99_SIGNATURE;
	hdr99->hdr.cksum = chrp_checksum(&hdr99->hdr);
	hdr99->adler = core99_calc_adler(nvram_image);
	core99_bank = core99_bank ? 0 : 1;
	if (core99_erase_bank(core99_bank)) {
		printk("nvram: Error erasing bank %d\n", core99_bank);
		return;
	}
	if (core99_write_bank(core99_bank, nvram_image))
		printk("nvram: Error writing bank %d\n", core99_bank);
}

unsigned char __pmac
nvram_read_byte(int addr)
{
	struct adb_request req;

	switch (nvram_naddrs) {
	case -1:
		if (pmu_request(&req, NULL, 3, PMU_READ_NVRAM,
				(addr >> 8) & 0xff, addr & 0xff))
			break;
		while (!req.complete)
			pmu_poll();
		return req.reply[1];
	case 1:
		if (is_core_99)
			return nvram_image ? nvram_image[addr] : 0;
		return nvram_data[(addr & (NVRAM_SIZE - 1)) * nvram_mult];
	case 2:
		*nvram_addr = addr >> 5;
		eieio();
		return nvram_data[(addr & 0x1f) << 4];
	}
	return 0;
}

void __pmac
nvram_write_byte(unsigned char val, int addr)
{
	struct adb_request req;

	switch (nvram_naddrs) {
	case -1:
		if (pmu_request(&req, NULL, 4, PMU_WRITE_NVRAM,
				(addr >> 8) & 0xff, addr & 0xff, val))
			break;
		while (!req.complete)
			pmu_poll();
		break;
	case 1:
		if (is_core_99) {
			if (!nvram_image)
				return;
			nvram_image[addr] = val;
			break;
		}
		nvram_data[(addr & (NVRAM_SIZE - 1)) * nvram_mult] = val;
		break;
	case 2:
		*nvram_addr = addr >> 5;
		eieio();
		nvram_data[(addr & 0x1f) << 4] = val;
		break;
	}
	eieio();
}

int __pmac
pmac_get_partition(int partition)
{
	return nvram_partitions[partition];
}

u8 __pmac
pmac_xpram_read(int xpaddr)
{
	int offset = nvram_partitions[pmac_nvram_XPRAM];
	
	if (offset < 0)
		return 0;
		
	return nvram_read_byte(xpaddr + offset);
}

void __pmac
pmac_xpram_write(int xpaddr, u8 data)
{
	int offset = nvram_partitions[pmac_nvram_XPRAM];
	
	if (offset < 0)
		return;
		
	nvram_write_byte(xpaddr + offset, data);
}

void __pmac
register_backlight_controller(struct backlight_controller *ctrler, void *data, char *type)
{
	struct device_node* bk_node;
	char *prop;
	int valid = 0;

	bk_node = find_devices("backlight");
	
	/* Special case for the old PowerBooks, they don't yet have
	 * the property we expect
	 */
	backlight_autosave = machine_is_compatible("AAPL,3400/2400")
		|| machine_is_compatible("AAPL,3500");
	if ((backlight_autosave
		|| machine_is_compatible("AAPL,PowerBook1998")
		|| machine_is_compatible("PowerBook1,1"))
		&& !strcmp(type, "pmu"))
		valid = 1;
	else if (bk_node) {
		prop = get_property(bk_node, "backlight-control", NULL);
		if (prop && !strncmp(prop, type, strlen(type)))
			valid = 1;
	}
	if (!valid)
		return;
	backlighter = ctrler;
	backlighter_data = data;
	
	if (bk_node && !backlight_autosave)
		prop = get_property(bk_node, "bklt", NULL);
	else
		prop = NULL;
	if (prop) {
		backlight_level = ((*prop)+1) >> 1;
		if (backlight_level > BACKLIGHT_MAX)
			backlight_level = BACKLIGHT_MAX;
	}
	
	if (backlight_autosave) {
		struct adb_request req;
		pmu_request(&req, NULL, 2, 0xd9, 0);
		while (!req.complete)
			pmu_poll();
		backlight_level = req.reply[1] >> 4;
	}

	if (!backlighter->set_enable(1, backlight_level, data))
		backlight_enabled = 1;

	printk(KERN_INFO "Registered \"%s\" backlight controller, level: %d/15\n",
		type, backlight_level);
}

void __pmac
unregister_backlight_controller(struct backlight_controller *ctrler, void *data)
{
	/* We keep the current backlight level (for now) */
	if (ctrler == backlighter && data == backlighter_data)
		backlighter = NULL;
}

int __pmac
set_backlight_enable(int enable)
{
	int rc;
	
	if (!backlighter)
		return -ENODEV;
	rc = backlighter->set_enable(enable, backlight_level, backlighter_data);
	if (!rc)
		backlight_enabled = enable;
	return rc;
}

int __pmac
get_backlight_enable(void)
{
	if (!backlighter)
		return -ENODEV;
	return backlight_enabled;
}

int __pmac
set_backlight_level(int level)
{
	int rc = 0;
	
	if (!backlighter)
		return -ENODEV;
	if (level < BACKLIGHT_MIN)
		level = BACKLIGHT_OFF;
	if (level > BACKLIGHT_MAX)
		level = BACKLIGHT_MAX;
	if (backlight_enabled)
		rc = backlighter->set_level(level, backlighter_data);
	if (!rc)
		backlight_level = level;
	if (!rc && !backlight_autosave) {
		level <<=1;
		if (level & 0x10)
			level |= 0x01;
		// -- todo: save to property "bklt"
	}
	return rc;
}

int __pmac
get_backlight_level(void)
{
	if (!backlighter)
		return -ENODEV;
	return backlight_level;
}

