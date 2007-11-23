/* $Id: fsm.c,v 1.4.2.4 1998/05/27 18:05:21 keil Exp $

 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *              based on the teles driver from Jan den Ouden
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 *
 * $Log: fsm.c,v $
 * Revision 1.4.2.4  1998/05/27 18:05:21  keil
 * HiSax 3.0
 *
 * Revision 1.4.2.3  1998/03/07 23:15:20  tsbogend
 * made HiSax working on Linux/Alpha
 *
 * Revision 1.4.2.2  1997/11/15 18:54:29  keil
 * cosmetics
 *
 * Revision 1.4.2.1  1997/10/17 22:13:49  keil
 * update to last hisax version
 *
 * Revision 1.6  1997/07/27 21:42:25  keil
 * proof Fsm routines
 *
 * Revision 1.5  1997/06/26 11:10:05  keil
 * Restart timer function added
 *
 * Revision 1.4  1997/04/06 22:56:42  keil
 * Some cosmetic changes
 *
 * Revision 1.3  1997/02/16 01:04:08  fritz
 * Bugfix: Changed timer handling caused hang with 2.1.X
 *
 * Revision 1.2  1997/01/09 20:57:27  keil
 * cleanup & FSM_TIMER_DEBUG
 *
 * Revision 1.1  1996/10/13 20:04:52  keil
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"

#define FSM_TIMER_DEBUG 0

HISAX_INITFUNC(void
FsmNew(struct Fsm *fsm,
       struct FsmNode *fnlist, int fncount))
{
	int i;

	fsm->jumpmatrix = (FSMFNPTR *)
		kmalloc(sizeof (FSMFNPTR) * fsm->state_count * fsm->event_count, GFP_KERNEL);
	memset(fsm->jumpmatrix, 0, sizeof (FSMFNPTR) * fsm->state_count * fsm->event_count);

	for (i = 0; i < fncount; i++) 
		if ((fnlist[i].state>=fsm->state_count) || (fnlist[i].event>=fsm->event_count)) {
			printk(KERN_ERR "FsmNew Error line %d st(%ld/%ld) ev(%ld/%ld)\n",
				i,(long)fnlist[i].state,(long)fsm->state_count,
				(long)fnlist[i].event,(long)fsm->event_count);
		} else		
			fsm->jumpmatrix[fsm->state_count * fnlist[i].event +
				fnlist[i].state] = (FSMFNPTR) fnlist[i].routine;
}

void
FsmFree(struct Fsm *fsm)
{
	kfree((void *) fsm->jumpmatrix);
}

int
FsmEvent(struct FsmInst *fi, int event, void *arg)
{
	FSMFNPTR r;
	char str[80];

	if ((fi->state>=fi->fsm->state_count) || (event >= fi->fsm->event_count)) {
		printk(KERN_ERR "FsmEvent Error st(%ld/%ld) ev(%d/%ld)\n",
			(long)fi->state,(long)fi->fsm->state_count,event,(long)fi->fsm->event_count);
		return(1);
	}
	r = fi->fsm->jumpmatrix[fi->fsm->state_count * event + fi->state];
	if (r) {
		if (fi->debug) {
			sprintf(str, "State %s Event %s",
				fi->fsm->strState[fi->state],
				fi->fsm->strEvent[event]);
			fi->printdebug(fi, str);
		}
		r(fi, event, arg);
		return (0);
	} else {
		if (fi->debug) {
			sprintf(str, "State %s Event %s no routine",
				fi->fsm->strState[fi->state],
				fi->fsm->strEvent[event]);
			fi->printdebug(fi, str);
		}
		return (!0);
	}
}

void
FsmChangeState(struct FsmInst *fi, int newstate)
{
	char str[80];

	fi->state = newstate;
	if (fi->debug) {
		sprintf(str, "ChangeState %s",
			fi->fsm->strState[newstate]);
		fi->printdebug(fi, str);
	}
}

static void
FsmExpireTimer(struct FsmTimer *ft)
{
#if FSM_TIMER_DEBUG
	if (ft->fi->debug) {
		char str[40];
		sprintf(str, "FsmExpireTimer %lx", (long) ft);
		ft->fi->printdebug(ft->fi, str);
	}
#endif
	FsmEvent(ft->fi, ft->event, ft->arg);
}

void
FsmInitTimer(struct FsmInst *fi, struct FsmTimer *ft)
{
	ft->fi = fi;
	ft->tl.function = (void *) FsmExpireTimer;
	ft->tl.data = (long) ft;
#if FSM_TIMER_DEBUG
	if (ft->fi->debug) {
		char str[40];
		sprintf(str, "FsmInitTimer %lx", (long) ft);
		ft->fi->printdebug(ft->fi, str);
	}
#endif
	init_timer(&ft->tl);
}

void
FsmDelTimer(struct FsmTimer *ft, int where)
{
#if FSM_TIMER_DEBUG
	if (ft->fi->debug) {
		char str[40];
		sprintf(str, "FsmDelTimer %lx %d", (long) ft, where);
		ft->fi->printdebug(ft->fi, str);
	}
#endif
	del_timer(&ft->tl);
}

int
FsmAddTimer(struct FsmTimer *ft,
	    int millisec, int event, void *arg, int where)
{

#if FSM_TIMER_DEBUG
	if (ft->fi->debug) {
		char str[40];
		sprintf(str, "FsmAddTimer %lx %d %d", (long) ft, millisec, where);
		ft->fi->printdebug(ft->fi, str);
	}
#endif

	if (ft->tl.next || ft->tl.prev) {
		printk(KERN_WARNING "FsmAddTimer: timer already active!\n");
		ft->fi->printdebug(ft->fi, "FsmAddTimer already active!");
		return -1;
	}
	init_timer(&ft->tl);
	ft->event = event;
	ft->arg = arg;
	ft->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&ft->tl);
	return 0;
}

void
FsmRestartTimer(struct FsmTimer *ft,
	    int millisec, int event, void *arg, int where)
{

#if FSM_TIMER_DEBUG
	if (ft->fi->debug) {
		char str[40];
		sprintf(str, "FsmRestartTimer %lx %d %d", (long) ft, millisec, where);
		ft->fi->printdebug(ft->fi, str);
	}
#endif

	if (ft->tl.next || ft->tl.prev)
		del_timer(&ft->tl);
	init_timer(&ft->tl);
	ft->event = event;
	ft->arg = arg;
	ft->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&ft->tl);
}

void
jiftime(char *s, long mark)
{
	s += 8;

	*s-- = '\0';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = '.';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 6 + '0';
	mark /= 6;
	*s-- = ':';
	*s-- = mark % 10 + '0';
	mark /= 10;
	*s-- = mark % 10 + '0';
}
