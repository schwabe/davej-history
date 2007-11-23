/*
 *	linux/arch/alpha/kernel/core_mcpcia.c
 *
 * Code common to all MCbus-PCI Adaptor core logic chipsets
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pci.h>
#include <asm/hwrpb.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_mcpcia.h>
#undef __EXTERN_INLINE

#include "machvec.h"
#include "proto.h"
#include "bios32.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#undef DEBUG_CFG

#ifdef DEBUG_CFG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

#define DEBUG_MCHECK 0	/* 0 = minimal, 1 = debug, 2 = dump */

static volatile unsigned int MCPCIA_mcheck_expected[NR_CPUS];
static volatile unsigned int MCPCIA_mcheck_taken[NR_CPUS];
static volatile unsigned int MCPCIA_mcheck_hose[NR_CPUS];
static unsigned int MCPCIA_jd[NR_CPUS];
static unsigned int MCPCIA_mcheck_enable_print = 1;
static unsigned int MCPCIA_mcheck_probing_hose = 0;

#define MCPCIA_MAX_HOSES	4

static struct linux_hose_info *mcpcia_hoses[MCPCIA_MAX_HOSES];

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the MCPCIA_HAXR2 register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | |D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:11	Device select bit.
 * 	10:8	Function number
 * 	 7:2	Register number
 *
 * Type 1:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | |B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|1|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:24	reserved
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 * Notes:
 *	The function number selects which function of a multi-function device 
 *	(e.g., SCSI and Ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */

static unsigned int
conf_read(unsigned long addr, unsigned char type1,
	  struct linux_hose_info *hose)
{
	unsigned long flags;
	unsigned long hoseno = hose->pci_hose_index;
	unsigned int stat0, value, temp, cpu;

	cpu = smp_processor_id();

	__save_and_cli(flags);

	DBG_CFG(("conf_read(addr=0x%lx, type1=%d, hose=%d)\n",
		 addr, type1, hoseno));

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(hoseno);
	*(vuip)MCPCIA_CAP_ERR(hoseno) = stat0; mb();
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno);
	DBG_CFG(("conf_read: MCPCIA CAP_ERR(%d) was 0x%x\n", hoseno, stat0));

	mb();
	draina();
	MCPCIA_mcheck_expected[cpu] = 1;
	MCPCIA_mcheck_taken[cpu] = 0;
	MCPCIA_mcheck_hose[cpu] = hoseno;
	mb();

	/* Access configuration space.  */
	value = *((vuip)addr);
	mb();
	mb();  /* magic */

	if (MCPCIA_mcheck_taken[cpu]) {
		MCPCIA_mcheck_taken[cpu] = 0;
		value = 0xffffffffU;
		mb();
	}
	MCPCIA_mcheck_expected[cpu] = 0;
	mb();

	DBG_CFG(("conf_read(): finished\n"));

	__restore_flags(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1,
	   struct linux_hose_info *hose)
{
	unsigned long flags;
	unsigned long hoseno = hose->pci_hose_index;
	unsigned int stat0, temp, cpu;

	cpu = smp_processor_id();

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(hoseno);
	*(vuip)MCPCIA_CAP_ERR(hoseno) = stat0; mb();
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno);
	DBG_CFG(("conf_write: MCPCIA CAP_ERR(%d) was 0x%x\n", hoseno, stat0));

	draina();
	MCPCIA_mcheck_expected[cpu] = 1;
	MCPCIA_mcheck_hose[cpu] = hoseno;
	mb();

	/* Access configuration space.  */
	*((vuip)addr) = value;
	mb();
	mb();  /* magic */
	temp = *(vuip)MCPCIA_CAP_ERR(hoseno); /* read to force the write */
	MCPCIA_mcheck_expected[cpu] = 0;
	mb();

	DBG_CFG(("conf_write(): finished\n"));
	__restore_flags(flags);
}

static int
mk_conf_addr(struct linux_hose_info *hose,
	     u8 bus, u8 device_fn, u8 where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	unsigned long addr;

	if (!pci_probe_enabled || !hose->pci_config_space)
		return -1;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
		 " pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

	/* Type 1 configuration cycle for *ALL* busses.  */
	*type1 = 1;

	if (hose->pci_first_busno == bus)
		bus = 0;
	addr = (bus << 16) | (device_fn << 8) | (where);
	addr <<= 5; /* swizzle for SPARSE */
	addr |= hose->pci_config_space;

	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

int
mcpcia_hose_read_config_byte (u8 bus, u8 device_fn, u8 where, u8 *value,
			      struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x00;
	*value = conf_read(addr, type1, hose) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_read_config_word (u8 bus, u8 device_fn, u8 where, u16 *value,
			      struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x08;
	*value = conf_read(addr, type1, hose) >> ((where & 3) * 8);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_read_config_dword (u8 bus, u8 device_fn, u8 where, u32 *value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x18;
	*value = conf_read(addr, type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_write_config_byte (u8 bus, u8 device_fn, u8 where, u8 value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x00;
	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_write_config_word (u8 bus, u8 device_fn, u8 where, u16 value,
			       struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x08;
	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

int
mcpcia_hose_write_config_dword (u8 bus, u8 device_fn, u8 where, u32 value,
				struct linux_hose_info *hose)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(hose, bus, device_fn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x18;
	conf_write(addr, value << ((where & 3) * 8), type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

static void
mcpcia_pci_clr_err(int cpu, int hose)
{
	MCPCIA_jd[cpu] = *(vuip)MCPCIA_CAP_ERR(hose);
	*(vuip)MCPCIA_CAP_ERR(hose) = 0xffffffff; mb(); /* clear them all */
	MCPCIA_jd[cpu] = *(vuip)MCPCIA_CAP_ERR(hose); /* read to force write */
}

void __init
mcpcia_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	struct linux_hose_info *hose;
	int h;

	/* Align memory to cache line; we'll be allocating from it.  */
	*mem_start = (*mem_start | 31) + 1;

	/* First, allocate for the maximum number of hoses we might have.  */
	for (h = 0; h < MCPCIA_MAX_HOSES; h++) {

		hose = (struct linux_hose_info *)*mem_start;
		*mem_start = (unsigned long)(hose + 1);

		memset(hose, 0, sizeof(*hose));

		mcpcia_hoses[h] = hose;

		hose->pci_config_space = MCPCIA_CONF(h);
		hose->pci_hose_index = h;
		hose->pci_first_busno = 255;
		hose->pci_last_busno = 0;

		/* Tell userland where I/O space is located.  */
		hose->pci_sparse_io_space = MCPCIA_IO(h) - IDENT_ADDR;
		hose->pci_sparse_mem_space = MCPCIA_SPARSE(h) - IDENT_ADDR;
		hose->pci_dense_io_space = 0;
		hose->pci_dense_mem_space = MCPCIA_DENSE(h) - IDENT_ADDR;
	}

#if 1
	printk("mcpcia_init_arch: allocating for %d hoses\n",
	       MCPCIA_MAX_HOSES);
#endif
}

/*
 * This is called from init_IRQ, since we cannot take interrupts
 * before then, so we cannot do this in init_arch.
 */
void __init
mcpcia_init_hoses(void)
{
	struct linux_hose_info *hose;
	unsigned int mcpcia_err;
	unsigned int pci_rev;
	int h, cpu;

	cpu = smp_processor_id();

	MCPCIA_mcheck_enable_print = 0;
	MCPCIA_mcheck_probing_hose = 1;

	/* First, find how many hoses we have.  */
	for (h = 0; h < MCPCIA_MAX_HOSES; h++) {

		/* Gotta be REAL careful.  If hose is absent, we get a
		   machine check.  */

		mb();
		mb();
		draina();
		wrmces(7);
		MCPCIA_mcheck_expected[cpu] = 1;
		MCPCIA_mcheck_taken[cpu]    = 0;
		MCPCIA_mcheck_hose[cpu] = h;
		pci_rev = 0xffffffff;
		mb();

		/* Access the bus revision word. */
		pci_rev = *(vuip)MCPCIA_REV(h);

#if 0
		draina(); /* huh? */
#endif

		mb();
		mb();  /* magic */
		if (MCPCIA_mcheck_taken[cpu]) {
			MCPCIA_mcheck_taken[cpu] = 0;
			pci_rev = 0xffffffff;
			mb();
			break;
		}
		MCPCIA_mcheck_expected[cpu] = 0;
		mb();

		if ((pci_rev >> 16) == PCI_CLASS_BRIDGE_HOST) {

			hose_count++;

			mcpcia_pci_clr_err(cpu, h);

			hose = mcpcia_hoses[h];

			*hose_tail = hose;
			hose_tail = &hose->next;
		}
	}

	MCPCIA_mcheck_enable_print = 1;
	MCPCIA_mcheck_probing_hose = 0;

#if 1
	printk("mcpcia_init_hoses: found %d hoses\n", hose_count);
#endif

	/* Now do init for each hose.  */
	for (hose = hose_head; hose; hose = hose->next) {
		h = hose->pci_hose_index;
#if 0
		printk("mcpcia_init_arch: -------- hose %d --------\n",h);
		printk("MCPCIA_REV 0x%x\n", *(vuip)MCPCIA_REV(h));
		printk("MCPCIA_WHOAMI 0x%x\n", *(vuip)MCPCIA_WHOAMI(h));
		printk("MCPCIA_HAE_MEM 0x%x\n", *(vuip)MCPCIA_HAE_MEM(h));
		printk("MCPCIA_HAE_IO 0x%x\n", *(vuip)MCPCIA_HAE_IO(h));
		printk("MCPCIA_HAE_DENSE 0x%x\n", *(vuip)MCPCIA_HAE_DENSE(h));
		printk("MCPCIA_INT_CTL 0x%x\n", *(vuip)MCPCIA_INT_CTL(h));
		printk("MCPCIA_INT_REQ 0x%x\n", *(vuip)MCPCIA_INT_REQ(h));
		printk("MCPCIA_INT_TARG 0x%x\n", *(vuip)MCPCIA_INT_TARG(h));
		printk("MCPCIA_INT_ADR 0x%x\n", *(vuip)MCPCIA_INT_ADR(h));
		printk("MCPCIA_INT_ADR_EXT 0x%x\n", *(vuip)MCPCIA_INT_ADR_EXT(h));
		printk("MCPCIA_INT_MASK0 0x%x\n", *(vuip)MCPCIA_INT_MASK0(h));
		printk("MCPCIA_INT_MASK1 0x%x\n", *(vuip)MCPCIA_INT_MASK1(h));
		printk("MCPCIA_HBASE 0x%x\n", *(vuip)MCPCIA_HBASE(h));
#endif

#if 1
		/* 
		 * Set up error reporting.
		 */
		mcpcia_err = *(vuip)MCPCIA_CAP_ERR(h);
		mcpcia_err |= 0x0006;   /* master/target abort */
		*(vuip)MCPCIA_CAP_ERR(h) = mcpcia_err;
		mb() ;
		mcpcia_err = *(vuip)MCPCIA_CAP_ERR(h);
#endif

		switch (alpha_use_srm_setup)
		{
		default:
#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM_SETUP)
			/* Check window 0 for enabled and mapped to 0. */
			if (((*(vuip)MCPCIA_W0_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T0_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W0_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W0_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W0_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 0 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W0_BASE(h),
				       *(vuip)MCPCIA_W0_MASK(h),
				       *(vuip)MCPCIA_T0_BASE(h));
#endif
				break;
			}

			/* Check window 1 for enabled and mapped to 0.  */
			if (((*(vuip)MCPCIA_W1_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T1_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W1_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W1_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W1_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 1 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W1_BASE(h),
				       *(vuip)MCPCIA_W1_MASK(h),
				       *(vuip)MCPCIA_T1_BASE(h));
#endif
				break;
			}

			/* Check window 2 for enabled and mapped to 0.  */
			if (((*(vuip)MCPCIA_W2_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T2_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W2_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W2_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W2_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 2 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W2_BASE(h),
				       *(vuip)MCPCIA_W2_MASK(h),
				       *(vuip)MCPCIA_T2_BASE(h));
#endif
				break;
			}

			/* Check window 3 for enabled and mapped to 0.  */
			if (((*(vuip)MCPCIA_W3_BASE(h) & 3) == 1)
			    && (*(vuip)MCPCIA_T3_BASE(h) == 0)
			    && ((*(vuip)MCPCIA_W3_MASK(h) & 0xfff00000U) > 0x0ff00000U)) {
				MCPCIA_DMA_WIN_BASE = *(vuip)MCPCIA_W3_BASE(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE = *(vuip)MCPCIA_W3_MASK(h) & 0xfff00000U;
				MCPCIA_DMA_WIN_SIZE += 0x00100000U;
#if 1
				printk("mcpcia_init_arch: using Window 3 settings\n");
				printk("mcpcia_init_arch: BASE 0x%x MASK 0x%x TRANS 0x%x\n",
				       *(vuip)MCPCIA_W3_BASE(h),
				       *(vuip)MCPCIA_W3_MASK(h),
				       *(vuip)MCPCIA_T3_BASE(h));
#endif
				break;
			}

			/* Otherwise, we must use our defaults.  */
			MCPCIA_DMA_WIN_BASE = MCPCIA_DMA_WIN_BASE_DEFAULT;
			MCPCIA_DMA_WIN_SIZE = MCPCIA_DMA_WIN_SIZE_DEFAULT;
#endif
		case 0:
			/*
			 * Set up the PCI->physical memory translation windows.
			 * For now, windows 2 and 3 are disabled. 
			 *
			 * Window 0 goes at 2 GB and is 2 GB large.
			 * Window 1 goes at ? MB and is ? MB large, S/G.
			 */

			*(vuip)MCPCIA_W0_BASE(h) = 1U | (MCPCIA_DMA_WIN_BASE_DEFAULT & 0xfff00000U);
			*(vuip)MCPCIA_W0_MASK(h) = (MCPCIA_DMA_WIN_SIZE_DEFAULT - 1) & 0xfff00000U;
			*(vuip)MCPCIA_T0_BASE(h) = 0;

			*(vuip)MCPCIA_W1_BASE(h) = 0x0 ;
			*(vuip)MCPCIA_W2_BASE(h) = 0x0 ;
			*(vuip)MCPCIA_W3_BASE(h) = 0x0 ;

			*(vuip)MCPCIA_HBASE(h) = 0x0 ;
			mb();
			break;
		}
#if 0
		{
			unsigned int mcpcia_int_ctl = *((vuip)MCPCIA_INT_CTL(h));
			printk("mcpcia_init_arch: INT_CTL was 0x%x\n", mcpcia_int_ctl);
			*(vuip)MCPCIA_INT_CTL(h) = 1U; mb();
			mcpcia_int_ctl = *(vuip)MCPCIA_INT_CTL(h);
		}
#endif

		/*
		 * Sigh... For the SRM setup, unless we know apriori what the HAE
		 * contents will be, we need to setup the arbitrary region bases
		 * so we can test against the range of addresses and tailor the
		 * region chosen for the SPARSE memory access.
		 *
		 * See include/asm-alpha/mcpcia.h for the SPARSE mem read/write.
		 */
		if (alpha_use_srm_setup) {
			unsigned int mcpcia_hae_mem = *(vuip)MCPCIA_HAE_MEM(h);

			alpha_mv.sm_base_r1 = (mcpcia_hae_mem      ) & 0xe0000000UL;
			alpha_mv.sm_base_r2 = (mcpcia_hae_mem << 16) & 0xf8000000UL;
			alpha_mv.sm_base_r3 = (mcpcia_hae_mem << 24) & 0xfc000000UL;

			/*
			 * Set the HAE cache, so that setup_arch() code
			 * will use the SRM setting always. Our readb/writeb
			 * code in mcpcia.h expects never to have to change
			 * the contents of the HAE.
			 */
			alpha_mv.hae_cache = mcpcia_hae_mem;

			alpha_mv.mv_readb = mcpcia_srm_readb;
			alpha_mv.mv_readw = mcpcia_srm_readw;
			alpha_mv.mv_writeb = mcpcia_srm_writeb;
			alpha_mv.mv_writew = mcpcia_srm_writew;
		} else {
			*(vuip)MCPCIA_HAE_MEM(h) = 0U; mb();
			*(vuip)MCPCIA_HAE_MEM(h); /* read it back. */
			*(vuip)MCPCIA_HAE_IO(h) = 0; mb();
			*(vuip)MCPCIA_HAE_IO(h);  /* read it back. */
		}
	}
}

static void
mcpcia_print_uncorrectable(struct el_MCPCIA_uncorrected_frame_mcheck *logout)
{
	struct el_common_EV5_uncorrectable_mcheck *frame;
	int i;

	frame = &logout->procdata;

	/* Print PAL fields */
	for (i = 0; i < 24; i += 2) {
		printk("  paltmp[%2d-%2d]    = %16lx %16lx\n",
		       i, i+1, frame->paltemp[i], frame->paltemp[i+1]);
	}
	for (i = 0; i < 8; i += 2) {
		printk("  shadow[%2d-%2d]     = %16lx %16lx\n",
		       i, i+1, frame->shadow[i], 
		       frame->shadow[i+1]);
	}
	printk("  Addr of excepting instruction  = %16lx\n",
	       frame->exc_addr);
	printk("  Summary of arithmetic traps    = %16lx\n",
	       frame->exc_sum);
	printk("  Exception mask                 = %16lx\n",
	       frame->exc_mask);
	printk("  Base address for PALcode       = %16lx\n",
	       frame->pal_base);
	printk("  Interrupt Status Reg           = %16lx\n",
	       frame->isr);
	printk("  CURRENT SETUP OF EV5 IBOX      = %16lx\n",
	       frame->icsr);
	printk("  I-CACHE Reg %s parity error    = %16lx\n",
	       (frame->ic_perr_stat & 0x800L) ? "Data" : "Tag", 
	       frame->ic_perr_stat); 
	printk("  D-CACHE error Reg              = %16lx\n",
	       frame->dc_perr_stat);
	if (frame->dc_perr_stat & 0x2) {
		switch (frame->dc_perr_stat & 0x03c) {
		case 8:
			printk("    Data error in bank 1\n");
			break;
		case 4:
			printk("    Data error in bank 0\n");
			break;
		case 20:
			printk("    Tag error in bank 1\n");
			break;
		case 10:
			printk("    Tag error in bank 0\n");
			break;
		}
	}
	printk("  Effective VA                   = %16lx\n",
	       frame->va);
	printk("  Reason for D-stream            = %16lx\n",
	       frame->mm_stat);
	printk("  EV5 SCache address             = %16lx\n",
	       frame->sc_addr);
	printk("  EV5 SCache TAG/Data parity     = %16lx\n",
	       frame->sc_stat);
	printk("  EV5 BC_TAG_ADDR                = %16lx\n",
	       frame->bc_tag_addr);
	printk("  EV5 EI_ADDR: Phys addr of Xfer = %16lx\n",
	       frame->ei_addr);
	printk("  Fill Syndrome                  = %16lx\n",
	       frame->fill_syndrome);
	printk("  EI_STAT reg                    = %16lx\n",
	       frame->ei_stat);
	printk("  LD_LOCK                        = %16lx\n",
	       frame->ld_lock);
}

void
mcpcia_print_system_area(unsigned long la_ptr)
{
	struct el_common *frame;
	int i;

	struct IOD_subpacket {
	  unsigned long base;
	  unsigned int whoami;
	  unsigned int rsvd1;
	  unsigned int pci_rev;
	  unsigned int cap_ctrl;
	  unsigned int hae_mem;
	  unsigned int hae_io;
	  unsigned int int_ctl;
	  unsigned int int_reg;
	  unsigned int int_mask0;
	  unsigned int int_mask1;
	  unsigned int mc_err0;
	  unsigned int mc_err1;
	  unsigned int cap_err;
	  unsigned int rsvd2;
	  unsigned int pci_err1;
	  unsigned int mdpa_stat;
	  unsigned int mdpa_syn;
	  unsigned int mdpb_stat;
	  unsigned int mdpb_syn;
	  unsigned int rsvd3;
	  unsigned int rsvd4;
	  unsigned int rsvd5;
	} *iodpp;

	frame = (struct el_common *)la_ptr;

	iodpp = (struct IOD_subpacket *) (la_ptr + frame->sys_offset);

	for (i = 0; i < hose_count; i++, iodpp++) {
	  printk("IOD %d Register Subpacket - Bridge Base Address %16lx\n",
		 i, iodpp->base);
	  printk("  WHOAMI      = %8x\n", iodpp->whoami);
	  printk("  PCI_REV     = %8x\n", iodpp->pci_rev);
	  printk("  CAP_CTRL    = %8x\n", iodpp->cap_ctrl);
	  printk("  HAE_MEM     = %8x\n", iodpp->hae_mem);
	  printk("  HAE_IO      = %8x\n", iodpp->hae_io);
	  printk("  INT_CTL     = %8x\n", iodpp->int_ctl);
	  printk("  INT_REG     = %8x\n", iodpp->int_reg);
	  printk("  INT_MASK0   = %8x\n", iodpp->int_mask0);
	  printk("  INT_MASK1   = %8x\n", iodpp->int_mask1);
	  printk("  MC_ERR0     = %8x\n", iodpp->mc_err0);
	  printk("  MC_ERR1     = %8x\n", iodpp->mc_err1);
	  printk("  CAP_ERR     = %8x\n", iodpp->cap_err);
	  printk("  PCI_ERR1    = %8x\n", iodpp->pci_err1);
	  printk("  MDPA_STAT   = %8x\n", iodpp->mdpa_stat);
	  printk("  MDPA_SYN    = %8x\n", iodpp->mdpa_syn);
	  printk("  MDPB_STAT   = %8x\n", iodpp->mdpb_stat);
	  printk("  MDPB_SYN    = %8x\n", iodpp->mdpb_syn);
	}
}

void
mcpcia_machine_check(unsigned long vector, unsigned long la_ptr,
		     struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_MCPCIA_uncorrected_frame_mcheck *mchk_logout;
	unsigned int cpu;
#if 0
halt();
#endif
	mb();
	mb();  /* magic */
#if 0
	draina();
#endif

	mchk_header = (struct el_common *)la_ptr;
	mchk_logout = (struct el_MCPCIA_uncorrected_frame_mcheck *)la_ptr;

	cpu = smp_processor_id();

	if (!MCPCIA_mcheck_probing_hose) {

	    if (MCPCIA_mcheck_expected[cpu])
		mcpcia_pci_clr_err(cpu, MCPCIA_mcheck_hose[cpu]);
	    else {
		/* FIXME: how do we figure out which hose the error was on? */
		mcpcia_pci_clr_err(cpu, 0);
		mcpcia_pci_clr_err(cpu, 1);
	    }

	} else {
		/* FIXME: clear out known always good hoses */
		mcpcia_pci_clr_err(cpu, 0);
		mcpcia_pci_clr_err(cpu, 1);
	}

	wrmces(0x7);
	mb();

	if (MCPCIA_mcheck_enable_print) {

	    process_mcheck_info(vector, la_ptr, regs, "MCPCIA",
				DEBUG_MCHECK, MCPCIA_mcheck_expected[cpu]);

	    if (vector != 0x620 && vector != 0x630
		&& ! MCPCIA_mcheck_expected[cpu]) {
		mcpcia_print_uncorrectable(mchk_logout);
		mcpcia_print_system_area(la_ptr);
	    }
	}

#if 0
	MCPCIA_mcheck_expected[cpu] = 0;
#endif
	MCPCIA_mcheck_taken[cpu] = 1;
}
