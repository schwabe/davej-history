/* $Id: config.c,v 1.15.2.18 1998/10/13 10:27:26 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 *
 * $Log: config.c,v $
 * Revision 1.15.2.18  1998/10/13 10:27:26  keil
 * New cards, minor fixes
 *
 * Revision 1.15.2.17  1998/10/11 19:31:31  niemann
 * Fixed problems with CONFIG_MODVERSIONS for sedlbauer cards
 *
 * Revision 1.15.2.16  1998/09/27 13:05:48  keil
 * Apply most changes from 2.1.X (HiSax 3.1)
 *
 * Revision 1.15.2.15  1998/09/12 18:43:56  niemann
 * Added new card: Sedlbauer ISDN-Controller PC/104
 *
 * Revision 1.15.2.14  1998/08/25 14:01:27  calle
 * Ported driver for AVM Fritz!Card PCI from the 2.1 tree.
 * I could not test it.
 *
 * Revision 1.15.2.13  1998/07/30 20:51:24  niemann
 * Fixed Sedlbauer Speed Card PCMCIA missing isdnl3new
 *
 * Revision 1.15.2.12  1998/07/15 14:43:29  calle
 * Support for AVM passive PCMCIA cards:
 *    A1 PCMCIA, FRITZ!Card PCMCIA and FRITZ!Card PCMCIA 2.0
 *
 * Revision 1.15.2.11  1998/05/27 18:05:07  keil
 * HiSax 3.0
 *
 * Revision 1.15.2.10  1998/04/11 18:43:13  keil
 * New cards
 *
 * Revision 1.15.2.9  1998/03/07 23:15:12  tsbogend
 * made HiSax working on Linux/Alpha
 *
 * Revision 1.15.2.8  1998/02/11 19:21:37  keil
 * fix typo
 *
 * Revision 1.15.2.7  1998/02/11 14:23:08  keil
 * support for Dr Neuhaus Niccy PnP and PCI
 *
 * Revision 1.15.2.6  1998/02/09 11:21:19  keil
 * Sedlbauer PCMCIA support from Marcus Niemann
 *
 * Revision 1.15.2.5  1998/01/27 23:28:48  keil
 * v2.8
 *
 * Revision 1.15.2.4  1998/01/27 22:33:53  keil
 * dynalink ----> asuscom
 *
 * Revision 1.15.2.3  1998/01/11 22:55:15  keil
 * 16.3c support
 *
 * Revision 1.15.2.2  1997/11/15 18:55:46  keil
 * New init, new cards
 *
 * Revision 1.15.2.1  1997/10/17 22:13:40  keil
 * update to last hisax version
 *
 * Revision 2.2  1997/09/11 17:24:46  keil
 * Add new cards
 *
 * Revision 2.1  1997/07/27 21:41:35  keil
 * version change
 *
 * Revision 2.0  1997/06/26 11:06:28  keil
 * New card and L1 interface.
 * Eicon.Diehl Diva and Dynalink IS64PH support
 *
 * old changes removed /KKe
 *
 */
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/config.h>
#include "hisax.h"
#include <linux/module.h>

/*
 * This structure array contains one entry per card. An entry looks
 * like this:
 *
 * { type, protocol, p0, p1, p2, NULL }
 *
 * type
 *    1 Teles 16.0       p0=irq p1=membase p2=iobase
 *    2 Teles  8.0       p0=irq p1=membase
 *    3 Teles 16.3       p0=irq p1=iobase
 *    4 Creatix PNP      p0=irq p1=IO0 (ISAC)  p2=IO1 (HSCX)
 *    5 AVM A1 (Fritz)   p0=irq p1=iobase
 *    6 ELSA PC          [p0=iobase] or nothing (autodetect)
 *    7 ELSA Quickstep   p0=irq p1=iobase
 *    8 Teles PCMCIA     p0=irq p1=iobase
 *    9 ITK ix1-micro    p0=irq p1=iobase
 *   10 ELSA PCMCIA      p0=irq p1=iobase
 *   11 Eicon.Diehl Diva p0=irq p1=iobase
 *   12 Asuscom ISDNLink p0=irq p1=iobase
 *   13 Teleint          p0=irq p1=iobase
 *   14 Teles 16.3c      p0=irq p1=iobase
 *   15 Sedlbauer speed  p0=irq p1=iobase
 *   16 USR Sportster internal  p0=irq  p1=iobase
 *   17 MIC card                p0=irq  p1=iobase
 *   18 ELSA Quickstep 1000PCI  no parameter
 *   19 Compaq ISDN S0 ISA card p0=irq  p1=IO0 (HSCX)  p2=IO1 (ISAC) p3=IO2
 *   20 Travers Technologies NETjet PCI card
 *   21 TELES PCI               no parameter
 *   22 Sedlbauer Speed Star    p0=irq p1=iobase
 *   23 reserved
 *   24 Dr Neuhaus Niccy PnP/PCI card p0=irq p1=IO0 p2=IO1 (PnP only)
 *   25 Teles S0Box             p0=irq p1=iobase (from isapnp setup)
 *   26 AVM A1 PCMCIA (Fritz)   p0=irq p1=iobase
 *   27 AVM PCI (Fritz!PCI)     no parameter
 *   28 Sedlbauer Speed Fax+ 	p0=irq p1=iobase (from isapnp setup)
 *
 * protocol can be either ISDN_PTYPE_EURO or ISDN_PTYPE_1TR6 or ISDN_PTYPE_NI1
 *
 *
 */

#ifdef CONFIG_HISAX_ELSA
#define DEFAULT_CARD ISDN_CTYPE_ELSA
#define DEFAULT_CFG {0,0,0,0}
int elsa_init_pcmcia(void*, int, int*, int);
#ifdef MODULE
static struct symbol_table hisax_syms_elsa = {
#include <linux/symtab_begin.h>
	X(elsa_init_pcmcia),
#include <linux/symtab_end.h>
};
void register_elsa_symbols(void) {
	register_symtab(&hisax_syms_elsa);
}
#endif
#endif
#ifdef CONFIG_HISAX_AVM_A1
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1
#define DEFAULT_CFG {10,0x340,0,0}
#endif

#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_A1_PCMCIA
#define DEFAULT_CFG {11,0x170,0,0}
int avm_a1_init_pcmcia(void*, int, int*, int);
void HiSax_closecard(int cardnr);
#ifdef MODULE
static struct symbol_table hisax_syms_avm_a1= {
#include <linux/symtab_begin.h>
	X(avm_a1_init_pcmcia),
	X(HiSax_closecard),
#include <linux/symtab_end.h>
};
void register_avm_a1_symbols(void) {
	register_symtab(&hisax_syms_avm_a1);
}
#endif
#endif
#ifdef CONFIG_HISAX_FRITZPCI
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_FRITZPCI
#define DEFAULT_CFG {0,0,0,0}
#endif
#ifdef CONFIG_HISAX_16_3
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_3
#define DEFAULT_CFG {15,0x180,0,0}
#endif
#ifdef CONFIG_HISAX_S0BOX
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_S0BOX
#define DEFAULT_CFG {7,0x378,0,0}
#endif
#ifdef CONFIG_HISAX_16_0
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_16_0
#define DEFAULT_CFG {15,0xd0000,0xd80,0}
#endif

#ifdef CONFIG_HISAX_TELESPCI
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELESPCI
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_IX1MICROR2
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_IX1MICROR2
#define DEFAULT_CFG {5,0x390,0,0}
#endif

#ifdef CONFIG_HISAX_DIEHLDIVA
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_DIEHLDIVA
#define DEFAULT_CFG {0,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_ASUSCOM
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_ASUSCOM
#define DEFAULT_CFG {5,0x200,0,0}
#endif

#ifdef CONFIG_HISAX_TELEINT
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELEINT
#define DEFAULT_CFG {5,0x300,0,0}
#endif

#ifdef CONFIG_HISAX_SEDLBAUER
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_SEDLBAUER
#define DEFAULT_CFG {11,0x270,0,0}
int sedl_init_pcmcia(void*, int, int*, int);
static struct symbol_table hisax_syms_sedl= {
#include <linux/symtab_begin.h>
	X(sedl_init_pcmcia),
#include <linux/symtab_end.h>
};
#endif

#ifdef CONFIG_HISAX_SPORTSTER
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_SPORTSTER
#define DEFAULT_CFG {7,0x268,0,0}
#endif

#ifdef CONFIG_HISAX_MIC
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_MIC
#define DEFAULT_CFG {12,0x3e0,0,0}
#endif

#ifdef CONFIG_HISAX_NETJET
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_NETJET
#define DEFAULT_CFG {0,0,0,0}
#endif

#ifdef CONFIG_HISAX_TELES3C
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_TELES3C
#define DEFAULT_CFG {5,0x500,0,0}
#endif

#ifdef CONFIG_HISAX_AMD7930
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_AMD7930
#define DEFAULT_CFG {12,0x3e0,0,0}
#endif

#ifdef CONFIG_HISAX_NICCY
#undef DEFAULT_CARD
#undef DEFAULT_CFG
#define DEFAULT_CARD ISDN_CTYPE_NICCY
#define DEFAULT_CFG {0,0x0,0,0}
#endif

#ifdef CONFIG_HISAX_1TR6
#define DEFAULT_PROTO ISDN_PTYPE_1TR6
#define DEFAULT_PROTO_NAME "1TR6"
#endif
#ifdef CONFIG_HISAX_EURO
#undef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_EURO
#undef DEFAULT_PROTO_NAME
#define DEFAULT_PROTO_NAME "EURO"
#endif
#ifdef CONFIG_HISAX_NI1
#undef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_NI1
#undef DEFAULT_PROTO_NAME
#define DEFAULT_PROTO_NAME "NI1"
#endif
#ifndef DEFAULT_PROTO
#define DEFAULT_PROTO ISDN_PTYPE_UNKNOWN
#define DEFAULT_PROTO_NAME "UNKNOWN"
#endif
#ifndef DEFAULT_CARD
#error "HiSax: No cards configured"
#endif

#define FIRST_CARD { \
  DEFAULT_CARD, \
  DEFAULT_PROTO, \
  DEFAULT_CFG, \
  NULL, \
}

#define EMPTY_CARD	{0, DEFAULT_PROTO, {0, 0, 0, 0}, NULL}

struct IsdnCard cards[] =
{
	FIRST_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
	EMPTY_CARD,
};

static char HiSaxID[64] HISAX_INITDATA = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" \
"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
char *HiSax_id HISAX_INITDATA = HiSaxID;
#ifdef MODULE
/* Variables for insmod */
static int type[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int protocol[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int io[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
#undef IO0_IO1
#ifdef CONFIG_HISAX_16_3
#define IO0_IO1
#endif
#ifdef CONFIG_HISAX_NICCY
#undef IO0_IO1
#define IO0_IO1
#endif
#ifdef IO0_IO1
static int io0[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int io1[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
#endif
static int irq[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static int mem[] HISAX_INITDATA =
{0, 0, 0, 0, 0, 0, 0, 0};
static char *id HISAX_INITDATA = HiSaxID;

#if (LINUX_VERSION_CODE > 0x020111)
MODULE_AUTHOR("Karsten Keil");
MODULE_PARM(type, "1-8i");
MODULE_PARM(protocol, "1-8i");
MODULE_PARM(io, "1-8i");
MODULE_PARM(irq, "1-8i");
MODULE_PARM(mem, "1-8i");
MODULE_PARM(id, "s");
#ifdef CONFIG_HISAX_16_3	/* For Creatix/Teles PnP */
MODULE_PARM(io0, "1-8i");
MODULE_PARM(io1, "1-8i");
#endif /* CONFIG_HISAX_16_3 */
#endif /* (LINUX_VERSION_CODE > 0x020111) */
#endif /* MODULE */

int nrcards;

extern char *l1_revision;
extern char *l2_revision;
extern char *l3_revision;
extern char *lli_revision;
extern char *tei_revision;

HISAX_INITFUNC(char *
HiSax_getrev(const char *revision))
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}

HISAX_INITFUNC(void
HiSaxVersion(void))
{
	char tmp[64];

	printk(KERN_INFO "HiSax: Linux Driver for passive ISDN cards\n");
	printk(KERN_INFO "HiSax: Version 3.0b\n");
	strcpy(tmp, l1_revision);
	printk(KERN_INFO "HiSax: Layer1 Revision %s\n", HiSax_getrev(tmp)); 
	strcpy(tmp, l2_revision);
	printk(KERN_INFO "HiSax: Layer2 Revision %s\n", HiSax_getrev(tmp)); 
	strcpy(tmp, tei_revision);
	printk(KERN_INFO "HiSax: TeiMgr Revision %s\n", HiSax_getrev(tmp)); 
	strcpy(tmp, l3_revision);
	printk(KERN_INFO "HiSax: Layer3 Revision %s\n", HiSax_getrev(tmp)); 
	strcpy(tmp, lli_revision);
	printk(KERN_INFO "HiSax: LinkLayer Revision %s\n", HiSax_getrev(tmp)); 
}

void
HiSax_mod_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
}

void
HiSax_mod_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

#ifdef MODULE
#define HiSax_init init_module
#else
__initfunc(void
HiSax_setup(char *str, int *ints))
{
	int i, j, argc;

	argc = ints[0];
	i = 0;
	j = 1;
	while (argc && (i < HISAX_MAX_CARDS)) {
		if (argc) {
			cards[i].typ = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].protocol = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[0] = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[1] = ints[j];
			j++;
			argc--;
		}
		if (argc) {
			cards[i].para[2] = ints[j];
			j++;
			argc--;
		}
		i++;
	}
	if (strlen(str)) {
		strcpy(HiSaxID, str);
		HiSax_id = HiSaxID;
	} else {
		strcpy(HiSaxID, "HiSax");
		HiSax_id = HiSaxID;
	}
}
#endif

__initfunc(int
HiSax_init(void))
{
	int i;
	
#ifdef MODULE
	int nzproto = 0;
#ifdef CONFIG_HISAX_ELSA
	if (type[0] == ISDN_CTYPE_ELSA_PCMCIA) {
		/* we have exported  and return in this case */
		return 0;
	}
#endif
#ifdef CONFIG_HISAX_SEDLBAUER
	if (type[0] == ISDN_CTYPE_SEDLBAUER_PCMCIA) {
		/* we have to export  and return in this case */
		register_symtab(&hisax_syms_sedl);
		return 0;
	}
#endif
#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
	if (type[0] == ISDN_CTYPE_A1_PCMCIA) {
		/* we have to export  and return in this case */
		register_avm_a1_symbols();
		return 0;
	}
#endif
#endif
	nrcards = 0;
	HiSaxVersion();
#ifdef MODULE
	if (id)			/* If id= string used */
		HiSax_id = id;
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
		switch (type[i]) {
			case ISDN_CTYPE_16_0:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = mem[i];
				cards[i].para[2] = io[i];
				break;

			case ISDN_CTYPE_8_0:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = mem[i];
				break;

#ifdef IO0_IO1
			case ISDN_CTYPE_PNP:
			case ISDN_CTYPE_NICCY:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io0[i];
				cards[i].para[2] = io1[i];
				break;
			case ISDN_CTYPE_COMPAQ_ISA:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io0[i];
				cards[i].para[2] = io1[i];
				cards[i].para[3] = io[i];
				break;
#endif
			case ISDN_CTYPE_ELSA:
				cards[i].para[0] = io[i];
				break;
			case ISDN_CTYPE_16_3:
			case ISDN_CTYPE_TELESPCMCIA:
			case ISDN_CTYPE_A1:
			case ISDN_CTYPE_A1_PCMCIA:
			case ISDN_CTYPE_ELSA_PNP:
			case ISDN_CTYPE_ELSA_PCMCIA:
			case ISDN_CTYPE_IX1MICROR2:
			case ISDN_CTYPE_DIEHLDIVA:
			case ISDN_CTYPE_ASUSCOM:
			case ISDN_CTYPE_TELEINT:
			case ISDN_CTYPE_SEDLBAUER:
			case ISDN_CTYPE_SEDLBAUER_PCMCIA:
			case ISDN_CTYPE_SEDLBAUER_FAX:
			case ISDN_CTYPE_SPORTSTER:
			case ISDN_CTYPE_MIC:
			case ISDN_CTYPE_TELES3C:
			case ISDN_CTYPE_S0BOX:
				cards[i].para[0] = irq[i];
				cards[i].para[1] = io[i];
				break;
			case ISDN_CTYPE_ELSA_PCI:
			case ISDN_CTYPE_NETJET:
			case ISDN_CTYPE_AMD7930:
			case ISDN_CTYPE_TELESPCI:
			case ISDN_CTYPE_FRITZPCI:
				break;
		}
	}
	if (!nzproto) {
		printk(KERN_WARNING "HiSax: Warning - no protocol specified\n");
		printk(KERN_WARNING "HiSax: Note! module load syntax has changed.\n");
		printk(KERN_WARNING "HiSax: using protocol %s\n", DEFAULT_PROTO_NAME);
	}
#endif
	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	CallcNew();
	Isdnl3New();
	Isdnl2New();
	TeiNew();
	Isdnl1New();
	if (HiSax_inithardware(NULL)) {
		/* Install only, if at least one card found */
		/* No symbols to export, hide all symbols */

#ifdef MODULE
		register_symtab(NULL);
		printk(KERN_INFO "HiSax: module installed\n");
#endif
		return (0);
	} else {
		Isdnl1Free();
		TeiFree();
		Isdnl2Free();
		Isdnl3Free();
		CallcFree();
		return -EIO;
	}
}

#ifdef MODULE
void
cleanup_module(void)
{
	int cardnr = nrcards -1;
	long flags;

	save_flags(flags);
	cli();
	while(cardnr>=0)
		HiSax_closecard(cardnr--);
	Isdnl1Free();
	TeiFree();
	Isdnl2Free();
	Isdnl3Free();
	CallcFree();
	restore_flags(flags);
	printk(KERN_INFO "HiSax module removed\n");
}

#ifdef CONFIG_HISAX_ELSA
int elsa_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
	int i;
	int nzproto = 0;

	nrcards = 0;
	HiSaxVersion();
	if (id)			/* If id= string used */
		HiSax_id = id;
	/* Initialize all 8 structs, even though we only accept
	   two pcmcia cards
	   */
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		cards[i].para[0] = irq[i];
		cards[i].para[1] = io[i];
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
	}
	cards[0].para[0] = pcm_irq;
	cards[0].para[1] = (int)pcm_iob;
	cards[0].protocol = prot;
	cards[0].typ = 10;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	Isdnl1New();
	CallcNew();
	Isdnl3New();
	Isdnl2New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
	return (0);
}
#endif
#endif

#ifdef CONFIG_HISAX_SEDLBAUER
int sedl_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
#ifdef MODULE
	int i;
	int nzproto = 0;

	nrcards = 0;
	HiSaxVersion();
	if (id)			/* If id= string used */
		HiSax_id = id;
	/* Initialize all 8 structs, even though we only accept
	   two pcmcia cards
	   */
	for (i = 0; i < HISAX_MAX_CARDS; i++) {
		cards[i].para[0] = irq[i];
		cards[i].para[1] = io[i];
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
	}
	cards[0].para[0] = pcm_irq;
	cards[0].para[1] = (int)pcm_iob;
	cards[0].protocol = prot;
	cards[0].typ = ISDN_CTYPE_SEDLBAUER_PCMCIA;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	CallcNew();
	Isdnl3New();
	Isdnl2New();
	Isdnl1New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
#endif
	return (0);
}
#endif

#ifdef MODULE
#ifdef CONFIG_HISAX_AVM_A1_PCMCIA
int avm_a1_init_pcmcia(void *pcm_iob, int pcm_irq, int *busy_flag, int prot)
{
	int i;
	int nzproto = 0;

	nrcards = 0;
	HiSaxVersion();
	if (id)			/* If id= string used */
		HiSax_id = id;
	/* Initialize all 16 structs, even though we only accept
	   two pcmcia cards
	   */
	for (i = 0; i < 16; i++) {
		cards[i].para[0] = irq[i];
		cards[i].para[1] = io[i];
		cards[i].typ = type[i];
		if (protocol[i]) {
			cards[i].protocol = protocol[i];
			nzproto++;
		}
	}
	cards[0].para[0] = pcm_irq;
	cards[0].para[1] = (int)pcm_iob;
	cards[0].protocol = prot;
	cards[0].typ = ISDN_CTYPE_A1_PCMCIA;
	nzproto = 1;

	if (!HiSax_id)
		HiSax_id = HiSaxID;
	if (!HiSaxID[0])
		strcpy(HiSaxID, "HiSax");
	for (i = 0; i < HISAX_MAX_CARDS; i++)
		if (cards[i].typ > 0)
			nrcards++;
	printk(KERN_DEBUG "HiSax: Total %d card%s defined\n",
	       nrcards, (nrcards > 1) ? "s" : "");

	Isdnl1New();
	CallcNew();
	Isdnl3New();
	Isdnl2New();
	TeiNew();
	HiSax_inithardware(busy_flag);
	printk(KERN_NOTICE "HiSax: module installed\n");
	return (0);
}
#endif
#endif 
