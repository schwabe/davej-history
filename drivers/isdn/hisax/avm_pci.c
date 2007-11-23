/* $Id: avm_pci.c,v 1.1.2.5 1998/10/13 18:38:50 keil Exp $

 * avm_pci.c    low level stuff for AVM Fritz!PCI isdn cards
 *              Thanks to AVM, Berlin for informations
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: avm_pci.c,v $
 * Revision 1.1.2.5  1998/10/13 18:38:50  keil
 * Fix PCI detection
 *
 * Revision 1.1.2.4  1998/10/04 23:03:41  keil
 * PCI has 255 device entries
 *
 * Revision 1.1.2.3  1998/09/27 23:52:57  keil
 * Fix error handling
 *
 * Revision 1.1.2.2  1998/09/27 13:03:16  keil
 * Fix segfaults on connect
 *
 * Revision 1.1.2.1  1998/08/25 14:01:24  calle
 * Ported driver for AVM Fritz!Card PCI from the 2.1 tree.
 * I could not test it.
 *
 * Revision 1.1  1998/08/20 13:47:30  keil
 * first version
 *
 *
 *
 */
#define __NO_VERSION__
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/interrupt.h>

extern const char *CardType[];
static const char *avm_pci_rev = "$Revision: 1.1.2.5 $";

#define  PCI_VENDOR_AVM		0x1244
#define  PCI_FRITZPCI_ID	0xa00

#define  HDLC_FIFO		0x0
#define  HDLC_STATUS		0x4

#define	 AVM_HDLC_1		0x00
#define	 AVM_HDLC_2		0x01
#define	 AVM_ISAC_FIFO		0x02
#define	 AVM_ISAC_REG_LOW	0x04
#define	 AVM_ISAC_REG_HIGH	0x06

#define  AVM_STATUS0_IRQ_ISAC	0x010000
#define  AVM_STATUS0_IRQ_HDLC	0x020000
#define  AVM_STATUS0_IRQ_TIMER	0x040000
#define  AVM_STATUS0_IRQ_MASK	0x070000

#define  AVM_STATUS0_RESET	0x01
#define  AVM_STATUS0_DIS_TIMER	0x02
#define  AVM_STATUS0_RES_TIMER	0x04
#define  AVM_STATUS0_ENA_IRQ	0x08
#define  AVM_STATUS0_TESTBIT	0x10

#define  AVM_STATUS1_INT_SEL	0x0f
#define  AVM_STATUS1_ENA_IOM	0x80

#define  HDLC_MODE_ITF_FLG	0x010000
#define  HDLC_MODE_TRANS	0x020000
#define  HDLC_MODE_CCR_7	0x040000
#define  HDLC_MODE_CCR_16	0x080000
#define  HDLC_MODE_TESTLOOP	0x800000

#define  HDLC_INT_XPR		0x80
#define  HDLC_INT_XDU		0x40
#define  HDLC_INT_RPR		0x20
#define  HDLC_INT_MASK		0xE0

#define  HDLC_STAT_RME		0x01
#define  HDLC_STAT_RDO		0x10
#define  HDLC_STAT_CRCVFRRAB	0x0E
#define  HDLC_STAT_CRCVFR	0x06
#define  HDLC_STAT_RML_MASK	0x3f00

#define  HDLC_CMD_XRS		0x80
#define  HDLC_CMD_XME		0x01
#define  HDLC_CMD_RRS		0x20
#define  HDLC_CMD_XML_MASK	0x3f00


/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	register u_int idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register u_char val;
	register long flags;
	
	save_flags(flags);
	cli();
	if (idx != inl(cs->hw.avm.cfg_reg + 4))
		outl(idx, cs->hw.avm.cfg_reg + 4);
	val = inb(cs->hw.avm.isac + (offset & 0xf));
	restore_flags(flags);
	return (val);
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	register u_int idx = (offset > 0x2f) ? AVM_ISAC_REG_HIGH : AVM_ISAC_REG_LOW;
	register long flags;
	
	save_flags(flags);
	cli();
	if (idx != inl(cs->hw.avm.cfg_reg + 4))
		outl(idx, cs->hw.avm.cfg_reg + 4);  
	outb(value, cs->hw.avm.isac + (offset & 0xf));
	restore_flags(flags);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	if (AVM_ISAC_FIFO != inl(cs->hw.avm.cfg_reg + 4))
		outl(AVM_ISAC_FIFO, cs->hw.avm.cfg_reg + 4);
	insb(cs->hw.avm.isac, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	if (AVM_ISAC_FIFO != inl(cs->hw.avm.cfg_reg + 4))
		outl(AVM_ISAC_FIFO, cs->hw.avm.cfg_reg + 4);
	outsb(cs->hw.avm.isac, data, size);
}

static inline u_int
ReadHDLC(struct IsdnCardState *cs, int chan, u_char offset)
{
	register u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register u_int val;
	register long flags;
	
	save_flags(flags);
	cli();
	if (idx != inl(cs->hw.avm.cfg_reg + 4))
		outl(idx, cs->hw.avm.cfg_reg + 4);
	val = inl(cs->hw.avm.isac + offset);
	restore_flags(flags);
	return (val);
}

static inline void
WriteHDLC(struct IsdnCardState *cs, int chan, u_char offset, u_int value)
{
	register u_int idx = chan ? AVM_HDLC_2 : AVM_HDLC_1;
	register long flags;
	
	save_flags(flags);
	cli();
	if (idx != inl(cs->hw.avm.cfg_reg + 4))
		outl(idx, cs->hw.avm.cfg_reg + 4);
	outl(value, cs->hw.avm.isac + offset);
	restore_flags(flags);
}

static u_char
ReadHDLC_s(struct IsdnCardState *cs, int chan, u_char offset)
{
	return(0xff & ReadHDLC(cs, chan, offset));
}

static void
WriteHDLC_s(struct IsdnCardState *cs, int chan, u_char offset, u_char value)
{
	WriteHDLC(cs, chan, offset, value);
}

static inline
struct BCState *Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return(&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return(&cs->bcs[1]);
	else
		return(NULL);
}

void inline
hdlc_sched_event(struct BCState *bcs, int event)
{
	bcs->event |= 1 << event;
	queue_task(&bcs->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

void
modehdlc(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;
	int hdlc = bcs->channel;

	if (cs->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "hdlc %c mode %d ichan %d",
			'A' + hdlc, mode, bc);
		debugl1(cs, tmp);
	}
	bcs->mode = mode;
	bcs->channel = bc;
	switch (mode) {
		case (L1_MODE_NULL):
			bcs->hw.hdlc.ctrl = HDLC_MODE_TRANS | HDLC_CMD_XRS | HDLC_CMD_RRS;
			WriteHDLC(cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
			break;
		case (L1_MODE_TRANS):
			bcs->hw.hdlc.ctrl = HDLC_MODE_TRANS | HDLC_CMD_XRS | HDLC_CMD_RRS;
			WriteHDLC(cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
			bcs->hw.hdlc.ctrl = HDLC_MODE_TRANS | HDLC_CMD_XRS;
			WriteHDLC(cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
			bcs->hw.hdlc.ctrl = HDLC_MODE_TRANS;
			hdlc_sched_event(bcs, B_XMTBUFREADY);
			break;
		case (L1_MODE_HDLC):
			bcs->hw.hdlc.ctrl = HDLC_MODE_ITF_FLG | HDLC_CMD_XRS | HDLC_CMD_RRS;
			WriteHDLC(cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
			bcs->hw.hdlc.ctrl = HDLC_MODE_ITF_FLG | HDLC_CMD_XRS;
			WriteHDLC(cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
			bcs->hw.hdlc.ctrl = HDLC_MODE_ITF_FLG;
			hdlc_sched_event(bcs, B_XMTBUFREADY);
			break;
	}
}

static inline void
hdlc_empty_fifo(struct BCState *bcs, int count)
{
	register u_int *ptr;
	u_char *p;
	u_int idx = bcs->channel ? AVM_HDLC_2 : AVM_HDLC_1;
	int cnt=0;
	struct IsdnCardState *cs = bcs->cs;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO)) {
		u_char tmp[32];
		sprintf(tmp, "hdlc_empty_fifo %d", count);
		debugl1(cs, tmp);
	}
	if (bcs->hw.hdlc.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hdlc_empty_fifo: incoming packet too large");
		return;
	}
	ptr = (u_int *) p = bcs->hw.hdlc.rcvbuf + bcs->hw.hdlc.rcvidx;
	bcs->hw.hdlc.rcvidx += count;
	if (idx != inl(cs->hw.avm.cfg_reg + 4))
		outl(idx, cs->hw.avm.cfg_reg + 4);
	while (cnt<count) {
		*ptr++ = inl(cs->hw.avm.isac);
		cnt += 4;
	}
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char tmp[256];
		char *t = tmp;

		t += sprintf(t, "hdlc_empty_fifo %c cnt %d",
			     bcs->channel ? 'B' : 'A', count);
		QuickHex(t, p, count);
		debugl1(cs, tmp);
	}
}

static inline void
hdlc_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int count, cnt =0;
	int fifo_size = 32;
	u_char *p;
	u_int *ptr;
	u_int idx = bcs->channel ? AVM_HDLC_2 : AVM_HDLC_1;


	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hdlc_fill_fifo");
	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;

	bcs->hw.hdlc.ctrl &= ~HDLC_CMD_XME;
	if (bcs->tx_skb->len > fifo_size) {
		count = fifo_size;
	} else {
		count = bcs->tx_skb->len;
		if (bcs->mode != L1_MODE_TRANS)
			bcs->hw.hdlc.ctrl |= HDLC_CMD_XME;
	}
	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO)) {
		u_char tmp[32];
		sprintf(tmp, "hdlc_fill_fifo %d/%ld", count, bcs->tx_skb->len);
		debugl1(cs, tmp);
	}
	ptr = (u_int *) p = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt -= count;
	bcs->hw.hdlc.count += count;
	if (idx != inl(cs->hw.avm.cfg_reg + 4))
		outl(idx, cs->hw.avm.cfg_reg + 4);
	bcs->hw.hdlc.ctrl &= ~HDLC_CMD_XML_MASK;
	bcs->hw.hdlc.ctrl |= ((count == fifo_size) ? 0 : count)<<8;
	outl(bcs->hw.hdlc.ctrl, cs->hw.avm.isac + HDLC_STATUS);
	while (cnt<count) {
		outl(*ptr++, cs->hw.avm.isac);
		cnt += 4;
	}
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char tmp[256];
		char *t = tmp;

		t += sprintf(t, "hdlc_fill_fifo %c cnt %d",
			     bcs->channel ? 'B' : 'A', count);
		QuickHex(t, p, count);
		debugl1(cs, tmp);
	}
}

static void
fill_hdlc(struct BCState *bcs)
{
	long flags;
	save_flags(flags);
	cli();
	hdlc_fill_fifo(bcs);
	restore_flags(flags);
}

static inline void
HDLC_irq(struct BCState *bcs, u_int stat) {
	u_char tmp[32];
	int len;
	struct sk_buff *skb;
	
	if (bcs->cs->debug & L1_DEB_HSCX) {
		sprintf(tmp, "ch%d stat %#x", bcs->channel, stat);
		debugl1(bcs->cs, tmp);
	}	
	if (stat & HDLC_INT_RPR) {
		if (stat & HDLC_STAT_RDO) {
			if (bcs->cs->debug & L1_DEB_HSCX) {
				debugl1(bcs->cs, "RDO");
			} else {
				sprintf(tmp, "ch%d stat %#x", bcs->channel, stat);
				debugl1(bcs->cs, tmp);
			}
			bcs->hw.hdlc.ctrl &= ~HDLC_STAT_RML_MASK;
			bcs->hw.hdlc.ctrl |= HDLC_CMD_RRS;
			WriteHDLC(bcs->cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
			bcs->hw.hdlc.ctrl &= ~HDLC_CMD_RRS;
			WriteHDLC(bcs->cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
			bcs->hw.hdlc.rcvidx = 0;
		} else {
			if (!(len = (stat & HDLC_STAT_RML_MASK)>>8))
				len = 32;
			hdlc_empty_fifo(bcs, len);
			if ((stat & HDLC_STAT_RME) || (bcs->mode == L1_MODE_TRANS)) {
				if (((stat & HDLC_STAT_CRCVFRRAB)==HDLC_STAT_CRCVFR) ||
					(bcs->mode == L1_MODE_TRANS)) {
					if (!(skb = dev_alloc_skb(bcs->hw.hdlc.rcvidx)))
						printk(KERN_WARNING "HDLC: receive out of memory\n");
					else {
						memcpy(skb_put(skb, bcs->hw.hdlc.rcvidx),
							bcs->hw.hdlc.rcvbuf, bcs->hw.hdlc.rcvidx);
						skb_queue_tail(&bcs->rqueue, skb);
					}
					bcs->hw.hdlc.rcvidx = 0;
					hdlc_sched_event(bcs, B_RCVBUFREADY);
				} else {
					if (bcs->cs->debug & L1_DEB_HSCX) {
						debugl1(bcs->cs, "invalid frame");
					} else {
						sprintf(tmp, "ch%d invalid frame %#x", bcs->channel, stat);
						debugl1(bcs->cs, tmp);
					}
					bcs->hw.hdlc.rcvidx = 0;
				} 
			}
		}
	}
	if (stat & HDLC_INT_XDU) {
		/* Here we lost an TX interrupt, so
		 * restart transmitting the whole frame.
		 */
		if (bcs->tx_skb) {
			skb_push(bcs->tx_skb, bcs->hw.hdlc.count);
			bcs->tx_cnt += bcs->hw.hdlc.count;
			bcs->hw.hdlc.count = 0;
//			hdlc_sched_event(bcs, B_XMTBUFREADY);
			sprintf(tmp, "ch%d XDU", bcs->channel);
		} else {
			sprintf(tmp, "ch%d XDU without skb", bcs->channel);
		}
		if (bcs->cs->debug & L1_DEB_WARN)
			debugl1(bcs->cs, tmp);
		bcs->hw.hdlc.ctrl &= ~HDLC_STAT_RML_MASK;
		bcs->hw.hdlc.ctrl |= HDLC_CMD_XRS;
		WriteHDLC(bcs->cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
		bcs->hw.hdlc.ctrl &= ~HDLC_CMD_XRS;
		WriteHDLC(bcs->cs, bcs->channel, HDLC_STATUS, bcs->hw.hdlc.ctrl);
		hdlc_fill_fifo(bcs);
	} else if (stat & HDLC_INT_XPR) {
		if (bcs->tx_skb) {
			if (bcs->tx_skb->len) {
				hdlc_fill_fifo(bcs);
				return;
			} else {
				if (bcs->st->lli.l1writewakeup &&
					(PACKET_NOACK != bcs->tx_skb->pkt_type))
					bcs->st->lli.l1writewakeup(bcs->st, bcs->hw.hdlc.count);
				dev_kfree_skb(bcs->tx_skb, FREE_WRITE);
				bcs->hw.hdlc.count = 0; 
				bcs->tx_skb = NULL;
			}
		}
		if ((bcs->tx_skb = skb_dequeue(&bcs->squeue))) {
			bcs->hw.hdlc.count = 0;
			test_and_set_bit(BC_FLG_BUSY, &bcs->Flag);
			hdlc_fill_fifo(bcs);
		} else {
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
			hdlc_sched_event(bcs, B_XMTBUFREADY);
		}
	}
}

inline void
HDLC_irq_main(struct IsdnCardState *cs)
{
	u_int stat;
	long  flags;
	struct BCState *bcs;

	save_flags(flags);
	cli();	
	stat = ReadHDLC(cs, 0, HDLC_STATUS);
	if (stat & HDLC_INT_MASK) {
		if (!(bcs = Sel_BCS(cs, 0))) {
			if (cs->debug)
				debugl1(cs, "hdlc spurious channel 0 IRQ");
		} else
			HDLC_irq(bcs, stat);
	}
	stat = ReadHDLC(cs, 1, HDLC_STATUS);
	if (stat & HDLC_INT_MASK) {
		if (!(bcs = Sel_BCS(cs, 1))) {
			if (cs->debug)
				debugl1(cs, "hdlc spurious channel 1 IRQ");
		} else
			HDLC_irq(bcs, stat);
	}
	restore_flags(flags);	
}

void
hdlc_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;
	long flags;

	switch (pr) {
		case (PH_DATA | REQUEST):
			save_flags(flags);
			cli();
			if (st->l1.bcs->tx_skb) {
				skb_queue_tail(&st->l1.bcs->squeue, skb);
				restore_flags(flags);
			} else {
				st->l1.bcs->tx_skb = skb;
				test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
				st->l1.bcs->hw.hdlc.count = 0;
				restore_flags(flags);
				st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			}
			break;
		case (PH_PULL | INDICATION):
			if (st->l1.bcs->tx_skb) {
				printk(KERN_WARNING "hdlc_l2l1: this shouldn't happen\n");
				break;
			}
			test_and_set_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			st->l1.bcs->tx_skb = skb;
			st->l1.bcs->hw.hdlc.count = 0;
			st->l1.bcs->cs->BC_Send_Data(st->l1.bcs);
			break;
		case (PH_PULL | REQUEST):
			if (!st->l1.bcs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				st->l1.l1l2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (PH_ACTIVATE | REQUEST):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			modehdlc(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			modehdlc(st->l1.bcs, 0, st->l1.bc);
			st->l1.l1l2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

void
close_hdlcstate(struct BCState *bcs)
{
	modehdlc(bcs, 0, 0);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.hdlc.rcvbuf) {
			kfree(bcs->hw.hdlc.rcvbuf);
			bcs->hw.hdlc.rcvbuf = NULL;
		}
		discard_queue(&bcs->rqueue);
		discard_queue(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb(bcs->tx_skb, FREE_WRITE);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

int
open_hdlcstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.hdlc.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for hdlc.rcvbuf\n");
			return (1);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->hw.hdlc.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

int
setstack_hdlc(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hdlcstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l2.l2l1 = hdlc_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

HISAX_INITFUNC(void
clear_pending_hdlc_ints(struct IsdnCardState *cs))
{
	int val;
	char tmp[64];

	val = ReadHDLC(cs, 0, HDLC_STATUS);
	sprintf(tmp, "HDLC 1 STA %x", val);
	debugl1(cs, tmp);
	val = ReadHDLC(cs, 1, HDLC_STATUS);
	sprintf(tmp, "HDLC 2 STA %x", val);
	debugl1(cs, tmp);
}

HISAX_INITFUNC(void 
inithdlc(struct IsdnCardState *cs))
{
	cs->bcs[0].BC_SetStack = setstack_hdlc;
	cs->bcs[1].BC_SetStack = setstack_hdlc;
	cs->bcs[0].BC_Close = close_hdlcstate;
	cs->bcs[1].BC_Close = close_hdlcstate;
	modehdlc(cs->bcs, 0, 0);
	modehdlc(cs->bcs + 1, 0, 0);
}

static void
avm_pci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, stat = 0;
	u_int sval;

	if (!cs) {
		printk(KERN_WARNING "AVM PCI: Spurious interrupt!\n");
		return;
	}
	sval = inl(cs->hw.avm.cfg_reg);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK)
		/* possible a shared  IRQ reqest */
		return;
	if (!(sval & AVM_STATUS0_IRQ_ISAC)) {
		val = ReadISAC(cs, ISAC_ISTA);
		isac_interrupt(cs, val);
		stat |= 2;
	}
	if (!(sval & AVM_STATUS0_IRQ_HDLC)) {
		HDLC_irq_main(cs);
	}
	if (stat & 2) {
		WriteISAC(cs, ISAC_MASK, 0xFF);
		WriteISAC(cs, ISAC_MASK, 0x0);
	}
}

static void
reset_avmpci(struct IsdnCardState *cs)
{
	long flags;

	save_flags(flags);
	sti();
	outb(AVM_STATUS0_RESET | AVM_STATUS0_DIS_TIMER, cs->hw.avm.cfg_reg + 2);
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
	schedule();
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER | AVM_STATUS0_ENA_IRQ, cs->hw.avm.cfg_reg + 2);
	outb(AVM_STATUS1_ENA_IOM, cs->hw.avm.cfg_reg + 3);
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + (10 * HZ) / 1000;	/* Timeout 10ms */
	schedule();
}

static int
AVM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			reset_avmpci(cs);
			return(0);
		case CARD_RELEASE:
			outb(0, cs->hw.avm.cfg_reg + 2);
			release_region(cs->hw.avm.cfg_reg, 32);
			return(0);
		case CARD_SETIRQ:
			return(request_irq(cs->irq, &avm_pci_interrupt,
					I4L_IRQ_FLAG | SA_SHIRQ, "HiSax", cs));
		case CARD_INIT:
			clear_pending_isac_ints(cs);
			initisac(cs);
			clear_pending_hdlc_ints(cs);
			inithdlc(cs);
			outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER,
				cs->hw.avm.cfg_reg + 2);
			WriteISAC(cs, ISAC_MASK, 0);
			outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER | 
				AVM_STATUS0_ENA_IRQ, cs->hw.avm.cfg_reg + 2);
			/* RESET Receiver and Transmitter */
			WriteISAC(cs, ISAC_CMDR, 0x41);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

static 	int pci_index __initdata = 0;

__initfunc(int
setup_avm_pci(struct IsdnCard *card))
{
	u_int val;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, avm_pci_rev);
	printk(KERN_INFO "HiSax: AVM PCI driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_FRITZPCI)
		return (0);
#if CONFIG_PCI
	for (pci_index = 0; pci_index < 255; pci_index++) {
		unsigned char pci_bus, pci_device_fn;
		unsigned int ioaddr;
		unsigned char irq;

		if (pcibios_find_device (PCI_VENDOR_AVM,
					PCI_FRITZPCI_ID, pci_index,
					&pci_bus, &pci_device_fn) != 0) {
			continue;
		}
		pcibios_read_config_byte(pci_bus, pci_device_fn,
				PCI_INTERRUPT_LINE, &irq);
		pcibios_read_config_dword(pci_bus, pci_device_fn,
				PCI_BASE_ADDRESS_1, &ioaddr);
		cs->irq = irq;
		cs->hw.avm.cfg_reg = ioaddr & PCI_BASE_ADDRESS_IO_MASK; 
		if (!cs->hw.avm.cfg_reg) {
			printk(KERN_WARNING "FritzPCI: No IO-Adr for PCI card found\n");
			return(0);
		}
		cs->hw.avm.isac = cs->hw.avm.cfg_reg + 0x10;
		break;
	}	
	if (pci_index == 255) {
		printk(KERN_WARNING "FritzPCI: No PCI card found\n");
		return(0);
        }
#else
	printk(KERN_WARNING "FritzPCI: NO_PCI_BIOS\n");
	return (0);
#endif /* CONFIG_PCI */

	if (check_region((cs->hw.avm.cfg_reg), 32)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.avm.cfg_reg,
		       cs->hw.avm.cfg_reg + 31);
		return (0);
	} else {
		request_region(cs->hw.avm.cfg_reg, 32, "avm PCI");
	}

	val = inl(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM PCI: stat %#x\n", val);
	printk(KERN_INFO "AVM PCI: Class %X Rev %d\n",
	       val & 0xff, (val>>8) & 0xff);

	printk(KERN_INFO
	       "HiSax: %s config irq:%d base:0x%X\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.avm.cfg_reg);

	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHDLC_s;
	cs->BC_Write_Reg = &WriteHDLC_s;
	cs->BC_Send_Data = &fill_hdlc;
	cs->cardmsg = &AVM_card_msg;
	ISACVersion(cs, "AVM PCI:");
	return (1);
}
