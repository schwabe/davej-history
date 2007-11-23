/*
 *    Disk Array driver for Compaq SMART2 Controllers
 *    Copyright 1998 Compaq Computer Corporation
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to arrays@compaq.com
 *
 *    If you want to make changes, improve or add functionality to this
 *    driver, you'll probably need the Compaq Array Controller Interface
 *    Specificiation (Document number ECG086/1198)
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/hdreg.h>
#include <asm/io.h>
#include <asm/bitops.h>

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#define DRIVER_NAME "Compaq SMART2 Driver (v 1.0)"

#define MAJOR_NR COMPAQ_SMART2_MAJOR
#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>

#include "cpqarray.h"
#include "ida_cmd.h"
#include "ida_ioctl.h"

#define READ_AHEAD	128
#define NR_CMDS	128 /* This can probably go as high as ~400 */

#define MAX_CTLR	8
#define CTLR_SHIFT	8

static int nr_ctlr = 0;
static ctlr_info_t *hba[MAX_CTLR] = { 0, 0, 0, 0, 0, 0, 0, 0 };

#ifdef CONFIG_BLK_CPQ_DA_EISA
#ifndef MODULE
static
#endif
int eisa[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#endif

static char *product_names[] = {
	"Unknown",
	"SMART-2/E",
	"SMART-2/P", /* or SMART-2DH */
	"SMART-2SL",
	"SMART-3200",
	"SMART-3100ES",
	"SMART-221",
};

static struct hd_struct * ida;
static int * ida_sizes;
static int * ida_blocksizes;
static int * ida_hardsizes;
static int * ida_maxsectors;
static int * ida_maxsegments;
static struct gendisk ida_gendisk[MAX_CTLR];


/* Debug... */
#define DBG(s)	s
/* Debug (general info)... */
#define DBGINFO(s)
/* Debug Paranoid... */
#define DBGP(s) s
/* Debug Extra Paranoid... */
#define DBGPX(s)

void cpqarray_init(void);
#ifdef CONFIG_BLK_CPQ_DA_PCI
static int cpqarray_pci_detect(void);
static void cpqarray_pci_init(ctlr_info_t *c, unchar bus, unchar device_fn);
static ulong remap_pci_mem(ulong base, ulong size);
#endif
#ifdef CONFIG_BLK_CPQ_DA_EISA
static int cpqarray_eisa_detect(void);
#endif
static void flushcomplete(int ctlr);
static int pollcomplete(int ctlr);
static void getgeometry(int ctlr);

static cmdlist_t * cmd_alloc(ctlr_info_t *h);
static void cmd_free(ctlr_info_t *h, cmdlist_t *c);

static int sendcmd(
	__u8	cmd,
	int	ctlr,
	void	*buff,
	size_t	size,
	unsigned int blk,
	unsigned int blkcnt,
	unsigned int log_unit );

static int ida_open(struct inode *inode, struct file *filep);
static void ida_release(struct inode *inode, struct file *filep);
static int ida_ioctl(struct inode *inode, struct file *filep, unsigned int cmd, unsigned long arg);
static int ida_ctlr_ioctl(int ctlr, int dsk, ida_ioctl_t *io);

static void do_ida_request(int i);
/*
 * This is a hack.  This driver eats a major number for each controller, and
 * sets blkdev[xxx].request_fn to each one of these so the real request
 * function knows what controller its working with.
 */
#define DO_IDA_REQUEST(x) { \
	int flags; save_flags(flags); cli(); \
	do_ida_request(x); restore_flags(flags); }

static void do_ida_request0(void) DO_IDA_REQUEST(0);
static void do_ida_request1(void) DO_IDA_REQUEST(1);
static void do_ida_request2(void) DO_IDA_REQUEST(2);
static void do_ida_request3(void) DO_IDA_REQUEST(3);
static void do_ida_request4(void) DO_IDA_REQUEST(4);
static void do_ida_request5(void) DO_IDA_REQUEST(5);
static void do_ida_request6(void) DO_IDA_REQUEST(6);
static void do_ida_request7(void) DO_IDA_REQUEST(7);

static void start_io(ctlr_info_t *h);

static inline cmdlist_t *removeQ(cmdlist_t **Qptr, cmdlist_t *c);
static inline void addQ(cmdlist_t **Qptr, cmdlist_t *c);
static inline void complete_buffers(struct buffer_head *bh, int ok);
static inline void complete_command(cmdlist_t *cmd, int timeout);

static void do_ida_intr(int irq, void *dev_id, struct pt_regs * regs);
static void ida_timer(unsigned long tdata);
static int frevalidate_logvol(kdev_t dev);
static int revalidate_logvol(kdev_t dev, int maxusage);
static int revalidate_allvol(kdev_t dev);

static void ida_procinit(int i);
static int ida_proc_get_info(char *buffer, char **start, off_t offset, int length, int dp);

/*
 * These macros control what happens when the driver tries to write to or
 * read from a card.  If the driver is configured for EISA only or PCI only,
 * the macros expand to inl/outl or readl/writel.  If the drive is configured
 * for both EISA and PCI, the macro expands to a conditional which uses
 * memory mapped IO if the card has it (PCI) or io ports if it doesn't (EISA).
 */
#ifdef CONFIG_BLK_CPQ_DA_PCI
#	ifdef CONFIG_BLK_CPQ_DA_EISA
#		define smart2_read(h, offset)  ( ((h)->vaddr) ? readl((h)->vaddr+(offset)) : inl((h)->ioaddr+(offset)) )
#		define smart2_write(p, h, offset) ( ((h)->vaddr) ? writel((p), (h)->vaddr+(offset)) : outl((p), (h)->ioaddr+(offset)) )
#	else
#		define smart2_read(h, offset)  readl((h)->vaddr+(offset))
#		define smart2_write(p, h, offset) writel((p), (h)->vaddr+(offset))
#	endif
#else
#	ifdef CONFIG_BLK_CPQ_DA_EISA
#		define smart2_read(h, offset)  inl((h)->vaddr+(offset))
#		define smart2_write(p, h, offset) outl((p), (h)->vaddr+(offset))
#	else
#		error "You must enable either SMART2 PCI support or SMART2 EISA support or both!"
#	endif
#endif

void ida_geninit(struct gendisk *g)
{
	int ctlr = g-ida_gendisk;
	int i,j;
	drv_info_t *drv;

	for(i=0; i<NWD; i++) {
		drv = &hba[ctlr]->drv[i];
		if (!drv->nr_blks)
			continue;
		ida[(ctlr<<CTLR_SHIFT) + (i<<NWD_SHIFT)].nr_sects =
		ida_sizes[(ctlr<<CTLR_SHIFT) + (i<<NWD_SHIFT)] =
				drv->nr_blks;

		for(j=0; j<16; j++) {
			ida_blocksizes[(ctlr<<CTLR_SHIFT) + (i<<NWD_SHIFT)+j] =
				1024;
			ida_hardsizes[(ctlr<<CTLR_SHIFT) + (i<<NWD_SHIFT)+j] =
				drv->blk_size;
		}
		ida_gendisk[ctlr].nr_real++;
	}

}

struct file_operations ida_fops  = {
	NULL,                        /* lseek - default */
	block_read,                  /* read - general block-dev read */
	block_write,                 /* write - general block-dev write */
	NULL,                        /* readdir - bad */
	NULL,                        /* select */
	ida_ioctl,                  /* ioctl */
	NULL,                        /* mmap */
	ida_open,                     /* open code */
	ida_release,                  /* release */
	block_fsync,	              /* fsync */
	NULL,                        /* fasync */
	NULL,			/* Disk change */
	frevalidate_logvol,	/* revalidate */
};


/*
 * Get us a file in /proc that says something about each controller.  Right
 * now, we add entries to /proc, but in the future we should probably get
 * our own subdir in /proc (/proc/array/ida) and put our stuff in there.
 */
extern struct inode_operations proc_diskarray_inode_operations;
struct proc_dir_entry *proc_array = NULL;
static void ida_procinit(int i)
{
	struct proc_dir_entry *pd;

	if (proc_array == NULL) {
		proc_array = kmalloc(sizeof(struct proc_dir_entry), GFP_KERNEL);
		if (!proc_array) return;
		memset(proc_array, 0, sizeof(struct proc_dir_entry));
		proc_array->namelen = 5;
		proc_array->name = "array";
		proc_array->mode = S_IFDIR | S_IRUGO | S_IXUGO;
		proc_array->nlink = 2;
		proc_array->uid = 0;
		proc_array->gid = 0;
		proc_array->size = 0;
		proc_array->ops = &proc_dir_inode_operations;
		proc_register_dynamic(&proc_root, proc_array);
	}

	pd = kmalloc(sizeof(struct proc_dir_entry), GFP_KERNEL);
	if (!pd) return;
	memset(pd, 0, sizeof(struct proc_dir_entry));
	pd->namelen = 4;
	pd->name = hba[i]->devname;
	pd->mode = S_IFREG | S_IRUGO;
	pd->nlink = 1;
	pd->uid = 0;
	pd->gid = 0;
	pd->size = 0;
	pd->ops = &proc_diskarray_inode_operations;
	pd->get_info = ida_proc_get_info;
	
	hba[i]->proc = (int)pd;
	proc_register_dynamic(proc_array, pd);
}

/*
 * Report information about this controller.
 */
static int ida_proc_get_info(char *buffer, char **start, off_t offset, int length, int pd)
{
	off_t pos = 0;
	off_t len = 0;
	int size, i, ctlr;
	ctlr_info_t *h;
	drv_info_t *drv;
#ifdef CPQ_PROC_PRINT_QUEUES
	cmdlist_t *c;
#endif

	for(ctlr=0; ctlr<nr_ctlr; ctlr++) 
		if (hba[ctlr] && hba[ctlr]->proc == pd) break;


	if ((h = hba[ctlr]) == NULL)
		return 0;

	size = sprintf(buffer, "%s:  Compaq %s Disk Array Controller\n"
		"       Board ID: %08lx\n"
		"       Firmware Revision: %c%c%c%c\n"
		"       Controller Sig: %08lx\n"
		"       Memory Address: %08lx\n"
		"       I/O Port: %04x\n"
		"       IRQ: %x\n"
		"       Logical drives: %d\n"
		"       Physical drives: %d\n\n"
		"       Current Q depth: %d\n"
		"       Max Q depth since init: %d\n\n",
		h->devname,
		product_names[h->product],
		(unsigned long)h->board_id,
		h->firm_rev[0], h->firm_rev[1], h->firm_rev[2], h->firm_rev[3],
		(unsigned long)h->ctlr_sig, (unsigned long)h->vaddr,
		(unsigned int) h->ioaddr, (unsigned int)h->intr,
		h->log_drives, h->phys_drives,
		h->Qdepth, h->maxQsinceinit);

	pos += size; len += size;
	
	size = sprintf(buffer+len, "Logical Drive Info:\n");
	pos += size; len += size;

	for(i=0; i<h->log_drives; i++) {
		drv = &h->drv[i];
		size = sprintf(buffer+len, "ida/c%dd%d: blksz=%d nr_blks=%d\n",
				ctlr, i, drv->blk_size, drv->nr_blks);
		pos += size; len += size;
	}

#ifdef CPQ_PROC_PRINT_QUEUES
	size = sprintf(buffer+len, "\nCurrent Queues:\n");
	pos += size; len += size;

	c = h->reqQ;
	size = sprintf(buffer+len, "reqQ = %p", c); pos += size; len += size;
	if (c) c=c->next;
	while(c && c != h->reqQ) {
		size = sprintf(buffer+len, "->%p", c);
		pos += size; len += size;
		c=c->next;
	}

	c = h->cmpQ;
	size = sprintf(buffer+len, "\ncmpQ = %p", c); pos += size; len += size;
	if (c) c=c->next;
	while(c && c != h->cmpQ) {
		size = sprintf(buffer+len, "->%p", c);
		pos += size; len += size;
		c=c->next;
	}

	size = sprintf(buffer+len, "\n"); pos += size; len += size;
#endif
	size = sprintf(buffer+len,"nr_allocs = %d\nnr_frees = %d\n",
			h->nr_allocs, h->nr_frees);
	pos += size; len += size;

	*start = buffer+offset;
	len -= offset;
	if (len>length)
		len = length;
	return len;
}

#ifdef MODULE
/* This is a hack... */
#include "proc_array.c"
int init_module(void)
{
	int i, j;
	cpqarray_init();
	if (nr_ctlr == 0)
		return -EIO;

	for(i=0; i<nr_ctlr; i++) {
		ida_geninit(&ida_gendisk[i]); 
		for(j=0; j<NWD; j++)	
			if (ida_sizes[(i<<CTLR_SHIFT) + (j<<NWD_SHIFT)])
				resetup_one_dev(&ida_gendisk[i], j);
	}
	return 0;
}
void cleanup_module(void)
{
	int i;
	struct gendisk *g;

	for(i=0; i<nr_ctlr; i++) {
		smart2_write(0, hba[i], INTR_MASK);
		free_irq(hba[i]->intr, hba[i]);
		vfree((void*)hba[i]->vaddr);
		unregister_blkdev(MAJOR_NR+i, hba[i]->devname);
		del_timer(&hba[i]->timer);
		proc_unregister(proc_array,
			((struct proc_dir_entry*)hba[i]->proc)->low_ino);
		kfree(hba[i]->cmd_pool);
		kfree(hba[i]->cmd_pool_bits);

		if (gendisk_head == &ida_gendisk[i]) {
			gendisk_head = ida_gendisk[i].next;
		} else {
			for(g=gendisk_head; g; g=g->next) {
				if (g->next == &ida_gendisk[i]) {
					g->next = ida_gendisk[i].next;
					break;
				}
			}
		}

		blk_dev[MAJOR_NR+i].request_fn = NULL;
		blksize_size[MAJOR_NR+i] = NULL;
		hardsect_size[MAJOR_NR+i] = NULL;
		max_sectors[MAJOR_NR+i] = NULL;
		max_segments[MAJOR_NR+i] = NULL;
	}
	proc_unregister(&proc_root, proc_array->low_ino);
	kfree(ida);
	kfree(ida_sizes);
	kfree(ida_hardsizes);
	kfree(ida_blocksizes);

	kfree(ida_maxsectors);
	kfree(ida_maxsegments);

}
#endif /* MODULE */

/*
 *  This is it.  Find all the controllers and register them.  I really hate
 *  stealing all these major device numbers.
 */
void cpqarray_init(void)
{
	void (*request_fns[MAX_CTLR])(void) = {
		do_ida_request0, do_ida_request1,
		do_ida_request2, do_ida_request3,
		do_ida_request4, do_ida_request5,
		do_ida_request6, do_ida_request7,
	};
	int i;

	/* detect controllers */
#ifdef CONFIG_BLK_CPQ_DA_PCI
	cpqarray_pci_detect();
#endif
#ifdef CONFIG_BLK_CPQ_DA_EISA
	cpqarray_eisa_detect();
#endif

	if (nr_ctlr == 0)
		return;

	printk(DRIVER_NAME "\n");
	printk("Found %d controller(s)\n", nr_ctlr);

	/* allocate space for disk structs */
	ida = kmalloc(sizeof(struct hd_struct)*nr_ctlr*NWD*16, GFP_KERNEL);
	ida_sizes =      kmalloc(sizeof(int)*nr_ctlr*NWD*16, GFP_KERNEL);
	ida_blocksizes = kmalloc(sizeof(int)*nr_ctlr*NWD*16, GFP_KERNEL);
	ida_hardsizes =  kmalloc(sizeof(int)*nr_ctlr*NWD*16, GFP_KERNEL);

	ida_maxsegments =  kmalloc(sizeof(int)*nr_ctlr*NWD*16, GFP_KERNEL);
	ida_maxsectors =  kmalloc(sizeof(int)*nr_ctlr*NWD*16, GFP_KERNEL);

	memset(ida, 0, sizeof(struct hd_struct)*nr_ctlr*NWD*16);
	memset(ida_sizes, 0, sizeof(int)*nr_ctlr*NWD*16);
	memset(ida_blocksizes, 0, sizeof(int)*nr_ctlr*NWD*16);
	memset(ida_hardsizes, 0, sizeof(int)*nr_ctlr*NWD*16);
	memset(ida_maxsegments, 0, sizeof(int)*nr_ctlr*NWD*16);
	memset(ida_maxsectors, 0, sizeof(int)*nr_ctlr*NWD*16);
	memset(ida_gendisk, 0, sizeof(struct gendisk)*MAX_CTLR);

	for(i=0; i<nr_ctlr*NWD*16; i++) {
		ida_maxsegments[i] = SG_MAX;
		ida_maxsectors[i] = 1024;
	}
	/* 
	 * register block devices
	 * Find disks and fill in structs
	 * Get an interrupt, set the Q depth and get into /proc
	 */
	for(i=0; i< nr_ctlr; i++) {
		smart2_write(0, hba[i], INTR_MASK);  /* No interrupts */
		if (request_irq(hba[i]->intr, do_ida_intr,
			SA_INTERRUPT | SA_SHIRQ, hba[i]->devname, hba[i])) {

			printk("Unable to get irq %d for %s\n",
				hba[i]->intr, hba[i]->devname);
			continue;
		}
		if (register_blkdev(MAJOR_NR+i, hba[i]->devname, &ida_fops)) {
			printk("Unable to get major number %d for %s\n",
				MAJOR_NR+i, hba[i]->devname);
			continue;
		}

		hba[i]->cmd_pool = (cmdlist_t*)kmalloc(
				NR_CMDS*sizeof(cmdlist_t), GFP_KERNEL);
		hba[i]->cmd_pool_bits = (__u32*)kmalloc(
				((NR_CMDS+31)/32)*sizeof(__u32), GFP_KERNEL);
		memset(hba[i]->cmd_pool, 0, NR_CMDS*sizeof(cmdlist_t));
		memset(hba[i]->cmd_pool_bits, 0, ((NR_CMDS+31)/32)*sizeof(__u32));

		printk("Finding drives on %s", hba[i]->devname);
		getgeometry(i);

		smart2_write(FIFO_NOT_EMPTY, hba[i], INTR_MASK);

		ida_procinit(i);

		ida_gendisk[i].major = MAJOR_NR + i;
		ida_gendisk[i].major_name = "ida";
		ida_gendisk[i].minor_shift = NWD_SHIFT;
		ida_gendisk[i].max_p = 16;
		ida_gendisk[i].max_nr = 16;
		ida_gendisk[i].init = ida_geninit;
		ida_gendisk[i].part = ida + (i*256);
		ida_gendisk[i].sizes = ida_sizes + (i*256);
		/* ida_gendisk[i].nr_real is handled by getgeometry */
	
		blk_dev[MAJOR_NR+i].request_fn = request_fns[i];
		blksize_size[MAJOR_NR+i] = ida_blocksizes + (i*256);
		hardsect_size[MAJOR_NR+i] = ida_hardsizes + (i*256);
		read_ahead[MAJOR_NR+i] = READ_AHEAD;
		max_sectors[MAJOR_NR+i] = ida_maxsectors + (i*256);
		max_segments[MAJOR_NR+i] = ida_maxsegments + (i*256);

		/* Get on the disk list */
		ida_gendisk[i].next = gendisk_head;
		gendisk_head = &ida_gendisk[i];

		init_timer(&hba[i]->timer);
		hba[i]->timer.expires = jiffies + IDA_TIMER;
		hba[i]->timer.data = (unsigned long)hba[i];
		hba[i]->timer.function = ida_timer;
		add_timer(&hba[i]->timer);

	}
	/* done ! */
}

#ifdef CONFIG_BLK_CPQ_DA_PCI
/*
 * Find the controller and initialize it
 */
static int cpqarray_pci_detect(void)
{
	int index;
	unchar bus=0, dev_fn=0;
	
	for(index=0; ; index++) {
		if (pcibios_find_device(PCI_VENDOR_ID_COMPAQ,
			PCI_DEVICE_ID_COMPAQ_SMART2P, index, &bus, &dev_fn))
			break;

		if (index == 1000000) break;
		if (nr_ctlr == 8) {
			printk("This driver supports a maximum of "
				"8 controllers.\n");
			break;
		}
		
		hba[nr_ctlr] = kmalloc(sizeof(ctlr_info_t), GFP_KERNEL);
		memset(hba[nr_ctlr], 0, sizeof(ctlr_info_t));
		cpqarray_pci_init(hba[nr_ctlr], bus, dev_fn);
		sprintf(hba[nr_ctlr]->devname, "ida%d", nr_ctlr);
		hba[nr_ctlr]->ctlr = nr_ctlr;
		nr_ctlr++;
	}

	return nr_ctlr;
}

/*
 * Find the IO address of the controller, its IRQ and so forth.  Fill
 * in some basic stuff into the ctlr_info_t structure.
 */
static void cpqarray_pci_init(ctlr_info_t *c, unchar bus, unchar device_fn)
{
	ushort vendor_id, device_id, command;
	unchar cache_line_size, latency_timer;
	unchar irq, revision;
	uint addr[6];

	int i;

	(void) pcibios_read_config_word(bus, device_fn,
					PCI_VENDOR_ID, &vendor_id);
	(void) pcibios_read_config_word(bus, device_fn,
					PCI_DEVICE_ID, &device_id);
	(void) pcibios_read_config_word(bus, device_fn,
					PCI_COMMAND, &command);
	for(i=0; i<6; i++)
		(void) pcibios_read_config_dword(bus, device_fn,
				PCI_BASE_ADDRESS_0 + i*4, addr+i);

	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_CLASS_REVISION,&revision);
	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_INTERRUPT_LINE, &irq);
	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_CACHE_LINE_SIZE, &cache_line_size);
	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_LATENCY_TIMER, &latency_timer);

DBGINFO(
	printk("vendor_id = %x\n", vendor_id);
	printk("device_id = %x\n", device_id);
	printk("command = %x\n", command);
	for(i=0; i<6; i++)
		printk("addr[%d] = %x\n", i, addr[i]);
	printk("revision = %x\n", revision);
	printk("irq = %x\n", irq);
	printk("cache_line_size = %x\n", cache_line_size);
	printk("latency_timer = %x\n", latency_timer);
);

	c->intr = irq;
	c->ioaddr = addr[0] & ~0x1;

	/*
	 * Memory base addr is first addr with the first bit _not_ set
	 */
	for(i=0; i<6; i++)
		if (!(addr[i] & 0x1)) {
			c->paddr = addr[i];
			break;
		}
	c->vaddr = remap_pci_mem(c->paddr, 128);
}

/*
 * Map (physical) PCI mem into (virtual) kernel space
 */
static ulong remap_pci_mem(ulong base, ulong size)
{
        ulong page_base        = ((ulong) base) & PAGE_MASK;
        ulong page_offs        = ((ulong) base) - page_base;
        ulong page_remapped    = (ulong) vremap(page_base, page_offs+size);

        return (ulong) (page_remapped ? (page_remapped + page_offs) : 0UL);
}
#endif /* CONFIG_BLK_CPQ_DA_PCI */

#ifdef CONFIG_BLK_CPQ_DA_EISA
/*
 * Copy the contents of the ints[] array passed to us by init.
 */
void cpqarray_setup(char *str, int *ints)
{
	int i;
	if (ints[0] & 1) {
		printk( "SMART2 Parameter Usage:\n"
			"     smart2=io,irq,io,irq,...\n");
		return;
	}
	for(i=0; i<ints[0]; i++) {
		eisa[i] = ints[i+1];
	}
}

/*
 * Find an EISA controller's signature.  Set up an hba if we find it.
 */
static int cpqarray_eisa_detect(void)
{
	int i=0;

	while(i<16 && eisa[i]) {
		if (inl(eisa[i]+0xC80) == 0x3040110e) {
			if (nr_ctlr == 8) {
				printk("This driver supports a maximum of "
					"8 controllers.\n");
				break;
			}
			hba[nr_ctlr] = kmalloc(sizeof(ctlr_info_t), GFP_KERNEL);
			memset(hba[nr_ctlr], 0, sizeof(ctlr_info_t));
			hba[nr_ctlr]->ioaddr = eisa[i];
			hba[nr_ctlr]->intr = eisa[i+1];
			sprintf(hba[nr_ctlr]->devname, "ida%d", nr_ctlr);
			hba[nr_ctlr]->ctlr = nr_ctlr;
			nr_ctlr++;
		} else {
			printk("SMART2:  Could not find a controller at io=0x%04x irq=0x%x\n", eisa[i], eisa[i+1]);
		}
		i+=2;
	}
	return nr_ctlr;
}
#endif /* CONFIG_BLK_CPQ_DA_EISA */

/*
 * Open.  Make sure the device is really there.
 */
static int ida_open(struct inode *inode, struct file *filep)
{
	int ctlr = MAJOR(inode->i_rdev) - MAJOR_NR;
	int dsk  = MINOR(inode->i_rdev) >> NWD_SHIFT;

	DBGINFO(printk("ida_open %x (%x:%x)\n", inode->i_rdev, ctlr, dsk) );
	if (ctlr > MAX_CTLR || hba[ctlr] == NULL)
		return -ENXIO;

	if (!suser() && ida_sizes[(ctlr << CTLR_SHIFT) +
						MINOR(inode->i_rdev)] == 0)
		return -ENXIO;

	/*
	 * Root is allowed to open raw volume zero even if its not configured
	 * so array config can still work.  I don't think I really like this,
	 * but I'm already using way to many device nodes to claim another one
	 * for "raw controller".
	 */
	if (suser()
		&& ida_sizes[(ctlr << CTLR_SHIFT) + MINOR(inode->i_rdev)] == 0 
		&& MINOR(inode->i_rdev) != 0)
		return -ENXIO;

	hba[ctlr]->drv[dsk].usage_count++;
	hba[ctlr]->usage_count++;
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * Close.  Sync first.
 */
void ida_release(struct inode *inode, struct file *filep)
{
	int ctlr = MAJOR(inode->i_rdev) - MAJOR_NR;
	int dsk  = MINOR(inode->i_rdev) >> NWD_SHIFT;

	DBGINFO(printk("ida_release %x (%x:%x)\n", inode->i_rdev, ctlr, dsk) );
	fsync_dev(inode->i_rdev);

	hba[ctlr]->drv[dsk].usage_count--;
	hba[ctlr]->usage_count--;
	MOD_DEC_USE_COUNT;
}

/*
 * Enqueuing and dequeuing functions for cmdlists.
 */
static inline void addQ(cmdlist_t **Qptr, cmdlist_t *c)
{
	if (*Qptr == NULL) {
		*Qptr = c;
		c->next = c->prev = c;
	} else {
		c->prev = (*Qptr)->prev;
		c->next = (*Qptr);
		(*Qptr)->prev->next = c;
		(*Qptr)->prev = c;
	}
}

static inline cmdlist_t *removeQ(cmdlist_t **Qptr, cmdlist_t *c)
{
	if (c && c->next != c) {
		if (*Qptr == c) *Qptr = c->next;
		c->prev->next = c->next;
		c->next->prev = c->prev;
	} else {
		*Qptr = NULL;
	}
	return c;
}

/*
 * Get a request and submit it to the controller.
 * This routine needs to grab all the requests it possibly can from the
 * req Q and submit them.  Interrupts are off (and need to be off) when you
 * are in here (either via the dummy do_ida_request functions or by being
 * called from the interrupt handler
 */
void do_ida_request(int ctlr)
{
	ctlr_info_t *h = hba[ctlr];
	cmdlist_t *c;
	int seg;
	char *lastdataend;
	struct buffer_head *bh;
	struct request *creq;

	creq = blk_dev[MAJOR_NR+ctlr].current_request;
	if (creq == NULL || creq->rq_status == RQ_INACTIVE)
		goto doreq_done;

	if (ctlr != MAJOR(creq->rq_dev)-MAJOR_NR ||
		ctlr > nr_ctlr || h == NULL) {
		printk("doreq cmd for %d, %x at %p\n",
				ctlr, creq->rq_dev, creq);
		complete_buffers(creq->bh, 0);
		goto doreq_done;
	}

	if ((c = cmd_alloc(h)) == NULL)
		goto doreq_done;

	blk_dev[MAJOR_NR+ctlr].current_request = creq->next;
	creq->rq_status = RQ_INACTIVE;

	bh = creq->bh;

	c->ctlr = ctlr;
	c->hdr.unit = MINOR(creq->rq_dev) >> NWD_SHIFT;
	c->hdr.size = sizeof(rblk_t) >> 2;
	c->size += sizeof(rblk_t);

	c->req.hdr.sg_cnt = creq->nr_segments;
	c->req.hdr.blk = ida[(ctlr<<CTLR_SHIFT) + MINOR(creq->rq_dev)].start_sect + creq->sector;
	c->req.hdr.blk_cnt = creq->nr_sectors;
	c->bh = bh;
	
	seg = 0; lastdataend = NULL;
	while(bh) {
		if (bh->b_data == lastdataend) {
			c->req.sg[seg-1].size += bh->b_size;
			lastdataend += bh->b_size;
		} else {
			c->req.sg[seg].size = bh->b_size;
			c->req.sg[seg].addr = (__u32) virt_to_bus(bh->b_data);
			lastdataend = bh->b_data + bh->b_size;
			if (seg++ > SG_MAX)
				panic("SG list overflow\n");
		}
		bh = bh->b_reqnext;
	}
	if (seg != creq->nr_segments)
		panic("seg != nr_segments\n");

	c->req.hdr.cmd = (creq->cmd == READ) ? IDA_READ : IDA_WRITE;
	c->type = CMD_RWREQ;

	/* Put the request on the tail of the request queue */
	addQ(&h->reqQ, c);
	h->Qdepth++;
	if (h->Qdepth > h->maxQsinceinit) h->maxQsinceinit = h->Qdepth;

	wake_up(&wait_for_request);
doreq_done:
	start_io(h);
}

/* 
 * start_io submits everything on a controller's request queue
 * and moves it to the completion queue.
 *
 * Interrupts had better be off if you're in here
 */
static void start_io(ctlr_info_t *h)
{
	cmdlist_t *c;

	while((c = h->reqQ) != NULL) {
		/* Can't do anything if we're busy */
		if (smart2_read(h, COMMAND_FIFO) == 0)
			return;

		/* Get the first entry from the request Q */
		removeQ(&h->reqQ, c);
		h->Qdepth--;
	
		/* Tell the controller to do our bidding */
		smart2_write(c->busaddr, h, COMMAND_FIFO);

		/* Get onto the completion Q */
		addQ(&h->cmpQ, c);
	}
}


static inline void complete_buffers(struct buffer_head *bh, int ok)
{
	struct buffer_head *xbh;
	while(bh) {
		xbh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		mark_buffer_uptodate(bh, ok);
		unlock_buffer(bh);
		bh = xbh;
	}
}

/*
 * Mark all buffers that cmd was responsible for
 */
static inline void complete_command(cmdlist_t *cmd, int timeout)
{
	char buf[80];
	int ok=1;

	if (cmd->req.hdr.rcode & RCODE_NONFATAL &&
		(hba[cmd->ctlr]->misc_tflags & MISC_NONFATAL_WARN) == 0) {
			sprintf(buf, "Non Fatal error on ida/c%dd%d\n",
					cmd->ctlr, cmd->hdr.unit);
			console_print(buf);
			hba[cmd->ctlr]->misc_tflags |= MISC_NONFATAL_WARN;
	}
	if (cmd->req.hdr.rcode & RCODE_FATAL) {
		sprintf(buf, "Fatal error on ida/c%dd%d\n",
				cmd->ctlr, cmd->hdr.unit);
		console_print(buf);
		ok = 0;
	}
	if (cmd->req.hdr.rcode & RCODE_INVREQ) {
sprintf(buf, "Invalid request on ida/c%dd%d = (cmd=%x sect=%d cnt=%d sg=%d ret=%x)\n",
				cmd->ctlr, cmd->hdr.unit, cmd->req.hdr.cmd,
				cmd->req.hdr.blk, cmd->req.hdr.blk_cnt,
				cmd->req.hdr.sg_cnt, cmd->req.hdr.rcode);
		console_print(buf);
		ok = 0;	
	}
	if (timeout) {
		sprintf(buf, "Request timeout on ida/c%dd%d\n",
				cmd->ctlr, cmd->hdr.unit);
		console_print(buf);
		ok = 0;
	}
	complete_buffers(cmd->bh, ok);
}

/*
 *  The controller will interrupt us upon completion of commands.
 *  Find the command on the completion queue, remove it, tell the OS and
 *  try to queue up more IO
 */
void do_ida_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	ctlr_info_t *h = dev_id;
	cmdlist_t *c;
	unsigned long istat;
	__u32 a,a1;

	istat = smart2_read(h, INTR_PENDING);
	/* Is this interrupt for us? */
	if (istat == 0)
		return;

	/*
	 * If there are completed commands in the completion queue,
	 * we had better do something about it.
	 */
	if (istat & FIFO_NOT_EMPTY) {
		while((a = smart2_read(h, COMMAND_COMPLETE_FIFO))) {
			a1 = a; a &= ~3;
			if ((c = h->cmpQ) == NULL) goto bad_completion;
			while(c->busaddr != a) {
				c = c->next;
				if (c == h->cmpQ) break;
			}
			/*
			 * If we've found the command, take it off the
			 * completion Q and free it
			 */
			if (c->busaddr == a) {
				removeQ(&h->cmpQ, c);
				if (c->type == CMD_RWREQ) {
					complete_command(c, 0);
					cmd_free(h, c);
				} else if (c->type == CMD_IOCTL_PEND) {
					c->type = CMD_IOCTL_DONE;
				}
				continue;
			}
bad_completion:
			printk("Completion of %08lx ignored\n", (unsigned long)a1);
		}
	}

	/*
	 * See if we can queue up some more IO (Is this safe?)
	 */
	do_ida_request(h->ctlr);
}

/*
 * This timer is for timing out requests that haven't happened after
 * IDA_TIMEOUT, or rather it _WAS_ for timing out "dead" requests.
 * That didn't work quite like I expected and would cause crashes
 * and other nonsense.
 */
static void ida_timer(unsigned long tdata)
{
	ctlr_info_t *h = (ctlr_info_t*)tdata;

	h->timer.expires = jiffies + IDA_TIMER;
	add_timer(&h->timer);
	h->misc_tflags = 0;
}

/*
 *  ida_ioctl does some miscellaneous stuff like reporting drive geometry,
 *  setting readahead and submitting commands from userspace to the controller.
 */
int ida_ioctl(struct inode *inode, struct file *filep, unsigned int cmd, unsigned long arg)
{
	int ctlr = MAJOR(inode->i_rdev) - MAJOR_NR;
	int dsk  = MINOR(inode->i_rdev) >> NWD_SHIFT;
	int error;
	int diskinfo[4];
	struct hd_geometry *geo = (struct hd_geometry *)arg;
	ida_ioctl_t *io = (ida_ioctl_t*)arg;
	ida_ioctl_t my_io;

	DBGINFO(printk("ida_ioctl %x %x %x\n", inode->i_rdev, cmd, arg));
	switch(cmd) {
	case HDIO_GETGEO:
		error = verify_area(VERIFY_WRITE, geo, sizeof(*geo));
		if (error) return error;
 		if (hba[ctlr]->drv[dsk].cylinders) {
 			diskinfo[0] = hba[ctlr]->drv[dsk].heads;
 			diskinfo[1] = hba[ctlr]->drv[dsk].sectors;
 			diskinfo[2] = hba[ctlr]->drv[dsk].cylinders;
 		} else {
 			diskinfo[0] = 0xff;
 			diskinfo[1] = 0x3f;
 			diskinfo[2] = hba[ctlr]->drv[dsk].nr_blks / (0xff*0x3f);
 		}
		put_user(diskinfo[0], &geo->heads);
		put_user(diskinfo[1], &geo->sectors);
		put_user(diskinfo[2], &geo->cylinders);
		put_user(ida[(ctlr<<CTLR_SHIFT)+MINOR(inode->i_rdev)].start_sect, &geo->start);
		return 0;
	case IDAGETDRVINFO:
		error = verify_area(VERIFY_WRITE, io, sizeof(*io));
		if (error) return error;
		memcpy_tofs(&io->c.drv,&hba[ctlr]->drv[dsk],sizeof(drv_info_t));
		return 0;
	case BLKGETSIZE:
		if (!arg) return -EINVAL;
		error = verify_area(VERIFY_WRITE, (long*)arg, sizeof(long));
		if (error) return error;
		put_user(ida[(ctlr<<CTLR_SHIFT)+MINOR(inode->i_rdev)].nr_sects, (long*)arg);
		return 0;
	case BLKRASET:
		if (!suser()) return -EACCES;
		if (!(inode->i_rdev)) return -EINVAL;
		if (arg>0xff) return -EINVAL;
		read_ahead[MAJOR(inode->i_rdev)] = arg;
		return 0;
	case BLKRAGET:
		if (!arg) return -EINVAL;
		error=verify_area(VERIFY_WRITE, (int*)arg, sizeof(int));
		if (error) return error;
		put_user(read_ahead[MAJOR(inode->i_rdev)], (int*)arg);
		return 0;
	case BLKRRPART:
		return revalidate_logvol(inode->i_rdev, 1);
	case IDAPASSTHRU:
		if (!suser()) return -EPERM;
		error = verify_area(VERIFY_READ|VERIFY_WRITE, io, sizeof(*io));
		if (error) return error;
		memcpy_fromfs(&my_io, io, sizeof(my_io));
		error = ida_ctlr_ioctl(ctlr, dsk, &my_io);
		if (error) return error;
		memcpy_tofs(io, &my_io, sizeof(my_io));
		return 0;
	case IDAGETCTLRSIG:
		if (!arg) return -EINVAL;
		error=verify_area(VERIFY_WRITE, (int*)arg, sizeof(int));
		if (error) return error;
		put_user(hba[ctlr]->ctlr_sig, (int*)arg);
		return 0;
	case IDAREVALIDATEVOLS:
		return revalidate_allvol(inode->i_rdev);

	RO_IOCTLS(inode->i_rdev, arg);

	default:
		return -EBADRQC;
	}
		
}
/*
 * ida_ctlr_ioctl is for passing commands to the controller from userspace.
 * The command block (io) has already been copied to kernel space for us,
 * however, any elements in the sglist need to be copied to kernel space
 * or copied back to userspace.
 *
 * Only root may perform a controller passthru command, however I'm not doing
 * any serious sanity checking on the arguments.  Doing an IDA_WRITE_MEDIA and
 * putting a 64M buffer in the sglist is probably a *bad* idea.
 */
int ida_ctlr_ioctl(int ctlr, int dsk, ida_ioctl_t *io)
{
	ctlr_info_t *h = hba[ctlr];
	cmdlist_t *c;
	void *p = NULL;
	unsigned long flags;
	int error;

	DBGINFO(printk("ida_ctlr_ioctl %d %x %p\n", ctlr, dsk, io));
	if ((c = cmd_alloc(NULL)) == NULL)
		return -ENOMEM;
	c->ctlr = ctlr;
	c->hdr.unit = (io->unit & UNITVALID) ? io->unit &0x7f : dsk;
	c->hdr.size = sizeof(rblk_t) >> 2;
	c->size += sizeof(rblk_t);
	c->req.hdr.cmd = io->cmd;
	c->type = CMD_IOCTL_PEND;

	/* Pre submit processing */
	switch(io->cmd) {
	case PASSTHRU_A:
		error = verify_area(VERIFY_READ|VERIFY_WRITE,
				(void*)io->sg[0].addr, io->sg[0].size);
		if (error) goto ioctl_err_exit;

		p = kmalloc(io->sg[0].size, GFP_KERNEL);
		if (!p) { error = -ENOMEM; goto ioctl_err_exit; }
		memcpy_fromfs(p, (void*)io->sg[0].addr, io->sg[0].size);
		c->req.bp = virt_to_bus(&(io->c));
		c->req.sg[0].size = io->sg[0].size;
		c->req.sg[0].addr = virt_to_bus(p);
		c->req.hdr.sg_cnt = 1;
		break;
	case IDA_READ:
		error = verify_area(VERIFY_WRITE,
				(void*)io->sg[0].addr, io->sg[0].size);
		if (error) goto ioctl_err_exit;
		p = kmalloc(io->sg[0].size, GFP_KERNEL);
		if (!p) { error = -ENOMEM; goto ioctl_err_exit; }
		c->req.sg[0].size = io->sg[0].size;
		c->req.sg[0].addr = virt_to_bus(p);
		c->req.hdr.sg_cnt = 1;
		break;
	case IDA_WRITE:
	case IDA_WRITE_MEDIA:
	case DIAG_PASS_THRU:
		error = verify_area(VERIFY_READ,
				(void*)io->sg[0].addr, io->sg[0].size);
		if (error) goto ioctl_err_exit;
		p = kmalloc(io->sg[0].size, GFP_KERNEL);
		if (!p) { error = -ENOMEM; goto ioctl_err_exit; }
		memcpy_fromfs(p, (void*)io->sg[0].addr, io->sg[0].size);
		c->req.sg[0].size = io->sg[0].size;
		c->req.sg[0].addr = virt_to_bus(p);
		c->req.hdr.sg_cnt = 1;
		break;
	default:
		c->req.sg[0].size = sizeof(io->c);
		c->req.sg[0].addr = virt_to_bus(&io->c);
		c->req.hdr.sg_cnt = 1;
	}

	/* Put the request on the tail of the request queue */
	save_flags(flags);
	cli();
	addQ(&h->reqQ, c);
	h->Qdepth++;
	start_io(h);
	restore_flags(flags);

	/* Wait for completion */
	while(c->type != CMD_IOCTL_DONE)
		schedule();

	/* Post submit processing */
	switch(io->cmd) {
	case PASSTHRU_A:
	case IDA_READ:
	case DIAG_PASS_THRU:
		memcpy_tofs((void*)io->sg[0].addr, p, io->sg[0].size);
		/* fall through and free p */
	case IDA_WRITE:
	case IDA_WRITE_MEDIA:
		kfree(p);
		break;
	default:
		/* Nothing to do */
	}

	io->rcode = c->req.hdr.rcode;
	error = 0;
ioctl_err_exit:
	cmd_free(NULL, c);
	return error;
}

/*
 * Commands are pre-allocated in a large block.  Here we use a simple bitmap
 * scheme to suballocte them to the driver.  Operations that are not time
 * critical (and can wait for kmalloc and possibly sleep) can pass in NULL
 * as the first argument to get a new command.
 */
cmdlist_t * cmd_alloc(ctlr_info_t *h)
{
	cmdlist_t * c;
	int i;

	if (h == NULL) {
		c = (cmdlist_t*)kmalloc(sizeof(cmdlist_t), GFP_KERNEL);
	} else {
		do {
			i = find_first_zero_bit(h->cmd_pool_bits, NR_CMDS);
			if (i == NR_CMDS)
				return NULL;
		} while(set_bit(i%32, h->cmd_pool_bits+(i/32)) != 0);
		c = h->cmd_pool + i;
		h->nr_allocs++;
	}

	memset(c, 0, sizeof(cmdlist_t));
	c->busaddr = virt_to_bus(c);
	return c;
}

void cmd_free(ctlr_info_t *h, cmdlist_t *c)
{
	int i;

	if (h == NULL) {
		kfree(c);
	} else {
		i = c - h->cmd_pool;
		clear_bit(i%32, h->cmd_pool_bits+(i/32));
		h->nr_frees++;
	}
}

/***********************************************************************
    name:        sendcmd
    Send a command to an IDA using the memory mapped FIFO interface
    and wait for it to complete.  
    This routine should only be called at init time.
***********************************************************************/
int sendcmd(
	__u8	cmd,
	int	ctlr,
	void	*buff,
	size_t	size,
	unsigned int blk,
	unsigned int blkcnt,
	unsigned int log_unit )
{
	cmdlist_t *c;
	int complete;
	__u32 base_ptr;
	unsigned long temp;
	unsigned long i;
	ctlr_info_t *info_p = hba[ctlr];

	c = cmd_alloc(info_p);
	c->ctlr = ctlr;
	c->hdr.unit = log_unit;
	c->hdr.prio = 0;
	c->hdr.size = sizeof(rblk_t) >> 2;
	c->size += sizeof(rblk_t);

	/* The request information. */
	c->req.hdr.next = 0;
	c->req.hdr.rcode = 0;
	c->req.bp = 0;
	c->req.hdr.sg_cnt = 1;
	c->req.hdr.reserved = 0;
	
	if (size == 0)
		c->req.sg[0].size = 512;
	else
		c->req.sg[0].size = size;

	c->req.hdr.blk = blk;
	c->req.hdr.blk_cnt = blkcnt;
	c->req.hdr.cmd = (unsigned char) cmd;
	c->req.sg[0].addr = (__u32) virt_to_bus(buff);
	flushcomplete(ctlr);
	/*
	 * Disable interrupt
	 */
	base_ptr = info_p->vaddr;
	smart2_write(0, info_p, INTR_MASK);
	/* Make sure there is room in the command FIFO */
	/* Actually it should be completely empty at this time. */
	for (i = 200000; i > 0; i--) {
		temp = smart2_read(info_p, COMMAND_FIFO);
		if (temp != 0) {
			break;
		}
		udelay(10);
DBG(
		printk("ida%d: idaSendPciCmd FIFO full, waiting!\n",
		       ctlr);
);
	}
	/*
	 * Send the cmd
	 */
	smart2_write(c->busaddr, info_p, COMMAND_FIFO);
	complete = pollcomplete(ctlr);
	if (complete != 1) {
		if (complete != c->busaddr) {
			printk(
			"ida%d: idaSendPciCmd "
		      "Invalid command list address returned! (%08lx)\n",
				ctlr, (unsigned long)complete);
			cmd_free(info_p, c);
			return (IO_ERROR);
		}
	} else {
		printk(
			"ida%d: idaSendPciCmd Timeout out, "
			"No command list address returned!\n",
			ctlr);
		cmd_free(info_p, c);
		return (IO_ERROR);
	}

	if (c->req.hdr.rcode & 0x00FE) {
		if (!(c->req.hdr.rcode & BIG_PROBLEM)) {
			printk(
			"ida%d: idaSendPciCmd, error: Controller failed "
				"at init time "
				"cmd: 0x%x, return code = 0x%x\n",
				ctlr, c->req.hdr.cmd, c->req.hdr.rcode);

			cmd_free(info_p, c);
			return (IO_ERROR);
		}
	}
	cmd_free(info_p, c);
	return (IO_OK);
}

int frevalidate_logvol(kdev_t dev)
{
	return revalidate_logvol(dev, 0);
}

/*
 * revalidate_allvol is for online array config utilities.  After a
 * utility reconfigures the drives in the array, it can use this function
 * (through an ioctl) to make the driver zap any previous disk structs for
 * that controller and get new ones.
 *
 * Right now I'm using the getgeometry() function to do this, but this
 * function should probably be finer grained and allow you to revalidate one
 * particualar logical volume (instead of all of them on a particular
 * controller).
 */
static int revalidate_allvol(kdev_t dev)
{
	int ctlr, i;
	unsigned long flags;

	ctlr = MAJOR(dev) - MAJOR_NR;
	if (MINOR(dev) != 0)
		return -ENXIO;

	save_flags(flags);
	cli();
	if (hba[ctlr]->usage_count > 1) {
		restore_flags(flags);
		printk("Device busy for volume revalidation (usage=%d)\n",
					hba[ctlr]->usage_count);
		return -EBUSY;
	}

	hba[ctlr]->usage_count++;
	restore_flags(flags);

	/*
	 * Set the partition and block size structures for all volumes
	 * on this controller to zero.  We will reread all of this data
	 */
	memset(ida+(ctlr*256),            0, sizeof(struct hd_struct)*NWD*16);
	memset(ida_sizes+(ctlr*256),      0, sizeof(int)*NWD*16);
	memset(ida_blocksizes+(ctlr*256), 0, sizeof(int)*NWD*16);
	memset(ida_hardsizes+(ctlr*256),  0, sizeof(int)*NWD*16);
	ida_gendisk[ctlr].nr_real = 0;

	/*
	 * Tell the array controller not to give us any interupts while
	 * we check the new geometry.  Then turn interrupts back on when
	 * we're done.
	 */
	smart2_write(0, hba[ctlr], INTR_MASK);
	getgeometry(ctlr);
	smart2_write(FIFO_NOT_EMPTY, hba[ctlr], INTR_MASK);

	ida_geninit(&ida_gendisk[ctlr]);
	for(i=0; i<NWD; i++)
		if (ida_sizes[(ctlr<<CTLR_SHIFT) + (i<<NWD_SHIFT)])
			revalidate_logvol(dev+(i<<NWD_SHIFT), 2);

	hba[ctlr]->usage_count--;
	return 0;
}

/* Borrowed and adapted from sd.c */
int revalidate_logvol(kdev_t dev, int maxusage)
{
	int ctlr, target;
	struct gendisk *gdev;
	unsigned long flags;
	int max_p;
	int start;
	int i;

	target = DEVICE_NR(dev);
	ctlr = MAJOR(dev) - MAJOR_NR;
	gdev = &ida_gendisk[ctlr];
	
	save_flags(flags);
	cli();
	if (hba[ctlr]->drv[target].usage_count > maxusage) {
		restore_flags(flags);
		printk("Device busy for revalidation (usage=%d)\n",
					hba[ctlr]->drv[target].usage_count);
		return -EBUSY;
	}

	hba[ctlr]->drv[target].usage_count++;
	restore_flags(flags);

	max_p = gdev->max_p;
	start = target << gdev->minor_shift;

	for(i=max_p; i>=0; i--) {
		int minor = start+i;
		kdev_t devi = MKDEV(MAJOR_NR + ctlr, minor);
		sync_dev(devi);
		invalidate_inodes(devi);
		invalidate_buffers(devi);
		gdev->part[minor].start_sect = 0;	
		gdev->part[minor].nr_sects = 0;	

		/* reset the blocksize so we can read the partition table */
		blksize_size[MAJOR_NR+ctlr][minor] = 1024;
	}

	gdev->part[start].nr_sects =  hba[ctlr]->drv[target].nr_blks;
	resetup_one_dev(gdev, target);
	hba[ctlr]->drv[target].usage_count--;
	return 0;
}


/********************************************************************
    name: pollcomplete
    Wait polling for a command to complete.
    The memory mapped FIFO is polled for the completion.
    Used only at init time, interrupts disabled.
 ********************************************************************/
int pollcomplete(int ctlr)
{
	int done;
	int i;

	/* Wait (up to 2 seconds) for a command to complete */

	for (i = 200000; i > 0; i--) {
		done = smart2_read(hba[ctlr], COMMAND_COMPLETE_FIFO);
		if (done == 0) {
			udelay(10);	/* a short fixed delay */
		} else
			return (done);
	}
	/* Invalid address to tell caller we ran out of time */
	return 1;
}

/*
 * Clear the complete FIFO

 Polling routine.
 This should only be used at init time.
 Any commands unexpectedly found in the completed command fifo
 will be discarded.  There should be none.
 Called in only one place.
 Note this reads and discards any completed commands but does not
 wait for any uncompleted commands.
 This is kinda goofy.

 */
void flushcomplete(int ctlr)
{
	unsigned long ret_addr;
	unsigned int i;

	for (i = 200000; i > 0; i--) {
		ret_addr = smart2_read(hba[ctlr], COMMAND_COMPLETE_FIFO);
		if (ret_addr == 0) {
			break;
		}
		udelay(10);
DBG(
		printk("ida%d: flushcomplete "
		       "Discarding completion %x!\n",
		       ctlr, (unsigned int)ret_addr);
);
	}
}



/*****************************************************************
    idaGetGeometry
    Get ida logical volume geometry from the controller 
    This is a large bit of code which once existed in two flavors,
    for EISA and PCI.  It is used only at init time.
****************************************************************
*/
void getgeometry(int ctlr)
{				
	id_log_drv_t *id_ldrive;
	id_ctlr_t *id_ctlr_buf;
	sense_log_drv_stat_t *id_lstatus_buf;
	config_t *sense_config_buf;
	unsigned int log_unit, log_index;
	int ret_code, size;
	drv_info_t *drv;
	ctlr_info_t *info_p = hba[ctlr];

	id_ldrive = (id_log_drv_t *)kmalloc(sizeof(id_log_drv_t), GFP_KERNEL);
	id_ctlr_buf = (id_ctlr_t *)kmalloc(sizeof(id_ctlr_t), GFP_KERNEL);
	id_lstatus_buf = (sense_log_drv_stat_t *)kmalloc(sizeof(sense_log_drv_stat_t), GFP_KERNEL);
	sense_config_buf = (config_t *)kmalloc(sizeof(config_t), GFP_KERNEL);

	memset(id_ldrive, 0, sizeof(id_log_drv_t));
	memset(id_ctlr_buf, 0, sizeof(id_ctlr_t));
	memset(id_lstatus_buf, 0, sizeof(sense_log_drv_stat_t));
	memset(sense_config_buf, 0, sizeof(config_t));

	info_p->phys_drives = 0;
	info_p->log_drv_map = 0;
	info_p->drv_assign_map = 0;
	info_p->drv_spare_map = 0;
	info_p->mp_failed_drv_map = 0;	/* only initialized here */
	/* Get controllers info for this logical drive */
	ret_code = sendcmd(ID_CTLR, ctlr, id_ctlr_buf, 0, 0, 0, 0);
	if (ret_code == IO_ERROR) {
		/*
		 * If can't get controller info, set the logical drive map to 0,
		 * so the idastubopen will fail on all logical drives
		 * on the controller.
		 */
		goto geo_ret;	/* release the buf and return */
	}
	info_p->log_drives = id_ctlr_buf->nr_drvs;;
	*(__u32*)(info_p->firm_rev) = *(__u32*)(id_ctlr_buf->firm_rev);
	info_p->ctlr_sig = id_ctlr_buf->cfg_sig;
	info_p->board_id = id_ctlr_buf->board_id;

	switch(info_p->board_id) {
		case 0x0E114030: /* SMART-2/E */
			info_p->product = 1;
			break;
		case 0x40300E11: /* SMART-2/P or SMART-2DH */
			info_p->product = 2;
			break;
		case 0x40310E11: /* SMART-2SL */
			info_p->product = 3;
			break;
		case 0x40320E11: /* SMART-3200 */
			info_p->product = 4;
			break;
		case 0x40330E11: /* SMART-3100ES */
			info_p->product = 5;
			break;
		case 0x40340E11: /* SMART-221 */
			info_p->product = 6;
			break;
		default:  
			/*
			 * Well, its a SMART-2 or better, don't know which
			 * kind.
 			 */
			info_p->product = 0;
	}
	printk(" (%s)\n", product_names[info_p->product]);
	/*
	 * Initialize logical drive map to zero
	 */
#ifdef REDUNDANT
	info_p->log_drive_map = 0;
#endif				/* #ifdef REDUNDANT */
	log_index = 0;
	/*
	 * Get drive geometry for all logical drives
	 */
	if (id_ctlr_buf->nr_drvs > 16)
		printk("ida%d:  This driver supports 16 logical drives "
			"per controller.  Additional drives will not be "
			"detected.\n", ctlr);

	for (log_unit = 0;
	     (log_index < id_ctlr_buf->nr_drvs)
	     && (log_unit < NWD);
	     log_unit++) {

		size = sizeof(sense_log_drv_stat_t);

		/*
		   Send "Identify logical drive status" cmd
		 */
		ret_code = sendcmd(SENSE_LOG_DRV_STAT,
			     ctlr, id_lstatus_buf, size, 0, 0, log_unit);
		if (ret_code == IO_ERROR) {
			/*
			   If can't get logical drive status, set
			   the logical drive map to 0, so the
			   idastubopen will fail for all logical drives
			   on the controller. 
			 */
			info_p->log_drv_map = 0;	
			printk(
			     "ida%d: idaGetGeometry - Controller failed "
				"to report status of logical drive %d\n"
			 "Access to this controller has been disabled\n",
				ctlr, log_unit);
			goto geo_ret;	/* release the buf and return */

		}
		/*
		   Make sure the logical drive is configured
		 */
		if (id_lstatus_buf->status != LOG_NOT_CONF) {
			ret_code = sendcmd(ID_LOG_DRV, ctlr, id_ldrive,
			       sizeof(id_log_drv_t), 0, 0, log_unit);
			/*
			   If error, the bit for this
			   logical drive won't be set and
			   idastubopen will return error. 
			 */
			if (ret_code != IO_ERROR) {
				drv = &info_p->drv[log_unit];
				drv->blk_size = id_ldrive->blk_size;
				drv->nr_blks = id_ldrive->nr_blks;
 				drv->cylinders = id_ldrive->drv.cyl;
 				drv->heads = id_ldrive->drv.heads;
 				drv->sectors = id_ldrive->drv.sect_per_track;
				info_p->log_drv_map |=	(1 << log_unit);

				printk("ida/c%dd%d: blksz=%d nr_blks=%d\n",
						ctlr, log_unit, drv->blk_size,
						drv->nr_blks);
				ret_code = sendcmd(SENSE_CONFIG,
						  ctlr, sense_config_buf,
				 sizeof(config_t), 0, 0, log_unit);
				if (ret_code == IO_ERROR) {
					info_p->log_drv_map = 0;
					goto geo_ret;	/* release the buf and return */
				}
				info_p->phys_drives =
				    sense_config_buf->ctlr_phys_drv;
				info_p->drv_assign_map
				    |= sense_config_buf->drv_asgn_map;
				info_p->drv_assign_map
				    |= sense_config_buf->spare_asgn_map;
				info_p->drv_spare_map
				    |= sense_config_buf->spare_asgn_map;
			}	/* end of if no error on id_ldrive */
			log_index = log_index + 1;
		}		/* end of if logical drive configured */
	}			/* end of for log_unit */
      geo_ret:
	kfree(id_ctlr_buf);
	kfree(id_ldrive);
	kfree(id_lstatus_buf);
	kfree(sense_config_buf);
}
