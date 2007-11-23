/* $Id: isdnl1.h,v 1.4.2.7 1998/11/03 00:06:55 keil Exp $

 * $Log: isdnl1.h,v $
 * Revision 1.4.2.7  1998/11/03 00:06:55  keil
 * certification related changes
 * fixed logging for smaller stack use
 *
 * Revision 1.4.2.6  1998/09/30 22:20:04  keil
 * Cosmetics
 *
 * Revision 1.4.2.5  1998/09/27 13:06:28  keil
 * Apply most changes from 2.1.X (HiSax 3.1)
 *
 * Revision 1.4.2.4  1998/05/27 18:05:49  keil
 * HiSax 3.0
 *
 * Revision 1.4.2.3  1997/12/01 09:09:08  keil
 * more l1 debug
 *
 * Revision 1.4.2.2  1997/11/15 18:50:40  keil
 * new common init function
 *
 * Revision 1.4.2.1  1997/10/17 22:13:58  keil
 * update to last hisax version
 *
 * Revision 2.2  1997/07/30 17:11:09  keil
 * L1deactivated exported
 *
 * Revision 2.1  1997/07/27 21:43:58  keil
 * new l1 interface
 *
 * Revision 2.0  1997/06/26 11:02:55  keil
 * New Layer and card interface
 *
 *
 */

#define D_RCVBUFREADY	0
#define D_XMTBUFREADY	1
#define D_L1STATECHANGE	2
#define D_CLEARBUSY	3
#define D_RX_MON0	4
#define D_RX_MON1	5
#define D_TX_MON0	6
#define D_TX_MON1	7

#define B_RCVBUFREADY 0
#define B_XMTBUFREADY 1

extern void debugl1(struct IsdnCardState *cs, char *fmt, ...);
extern void DChannel_proc_xmt(struct IsdnCardState *cs);
extern void DChannel_proc_rcv(struct IsdnCardState *cs);
extern void l1_msg(struct IsdnCardState *cs, int pr, void *arg);
extern void l1_msg_b(struct PStack *st, int pr, void *arg);

#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir);
#endif
