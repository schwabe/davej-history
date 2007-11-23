/*
 * Copyright 1998, 1999, 2000 Xpeed, Inc.
 * xpds-fsm.c, $Revision: 1.2 $
 * License to copy and distribute is GNU General Public License, version 2.
 */
#ifdef DEBUG
#define dprintk         if (xpds_debug_level & DEBUG_FSM) printk
#else
#define dprintk         if (0) printk
#endif
 
#ifndef __KERNEL__
#define __KERNEL__ 1
#endif
#ifndef MODULE
#define MODULE 1
#endif

#define __NO_VERSION__ 1
#include <linux/module.h>
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>

#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>

#include <asm/system.h>
#include <asm/segment.h>

#if ! defined (CONFIG_PCI)
#error "CONFIG_PCI is not defined"
#endif

#include "xpds-reg.h"
#include "xpds-softnet.h"
#include "xpds.h"

/*
 * Time periods in milliseconds.
 */
#define JIFFIES_COUNT(t)	((t)*HZ/1000?(t)*HZ/1000:1)

#define T1 	JIFFIES_COUNT(15000)
#define T2 	JIFFIES_COUNT(3)
#define T3 	JIFFIES_COUNT(40)
#define T4 	JIFFIES_COUNT(6000)
#define T5 	JIFFIES_COUNT(1000)
#define T6 	JIFFIES_COUNT(6000)
#define T7 	JIFFIES_COUNT(40)
#define T8 	JIFFIES_COUNT(24)
#define T9 	JIFFIES_COUNT(40)
#define T10 	JIFFIES_COUNT(40)
#define T11 	JIFFIES_COUNT(9)
#define T12	JIFFIES_COUNT(5500)
#define T13	JIFFIES_COUNT(15000)
#define T14	JIFFIES_COUNT(1/2)

static unsigned long	timer_limits[15] = {
	0, T1, T2, T3, T4, T5, T6, T7,
	T8, T9, T10, T11, T12, T13, T14
};

static unsigned long	timers[15];

#define MODE_LT	0
#define MODE_NT	1

/*
 * States
 */
#define	FSM_LT_RECEIVE_RESET	0
#define	FSM_LT_AWAKE_ERROR	1
#define	FSM_LT_DEACTIVATED	2
#define	FSM_LT_RESET_FOR_LOOP	3
#define	FSM_LT_ALERTING		4
#define	FSM_LT_WAIT_FOR_TN	5
#define	FSM_LT_AWAKE		6
#define	FSM_LT_EC_TRAINING	7
#define	FSM_LT_EC_CONVERGED	8
#define	FSM_LT_EQ_TRAINING	9
#define	FSM_LT_LINE_ACTIVE	10
#define	FSM_LT_PENDING_TRANSPARENT	11
#define FSM_LT_TRANSPARENT	12
#define FSM_LT_ST_DEACTIVATED	13
#define	FSM_LT_PENDING_DEACTIVATION	14
#define	FSM_LT_LOSS_OF_SYNC	15
#define	FSM_LT_LOSS_OF_SIGNAL	16
#define	FSM_LT_TEAR_DOWN_ERROR	17
#define FSM_LT_TEST		18
#define FSM_LT_TEAR_DOWN	19

#define	FSM_NT_RECEIVE_RESET	0
#define	FSM_NT_PENDING_TIMING	1
#define	FSM_NT_DEACTIVATED	2
#define	FSM_NT_IOM_AWAKED	3
#define	FSM_NT_ALERTING		4
#define	FSM_NT_EC_TRAINING	5
#define	FSM_NT_EQ_TRAINING	6
#define	FSM_NT_WAIT_FOR_SF	7
#define	FSM_NT_SYNCHRONIZED_1	8
#define	FSM_NT_SYNCHRONIZED_2	9
#define	FSM_NT_WAIT_FOR_ACT	10
#define	FSM_NT_TRANSPARENT	11
#define FSM_NT_ALERTING_1	12
#define FSM_NT_EC_TRAINING_1	13
#define	FSM_NT_PEND_DEACT_ST	14
#define	FSM_NT_ERROR_ST		15
#define	FSM_NT_PEND_RECEIVE_RES	16
#define	FSM_NT_PEND_DEACT_U	17
#define FSM_NT_TEST		18
#define FSM_NT_EC_TRAINING_AL	19
#define FSM_NT_WAIT_FOR_SF_AL	20
#define FSM_NT_ANALOG_LOOP_BACK	21

static int	lt_state = FSM_LT_TEST;
static int	nt_state = FSM_NT_RECEIVE_RESET;

/*
 * Commands (written to TXCI in MCR)
 */
#define CMD_NULL		(~0)

#define	CMD_LT_DR		0x00
#define	CMD_LT_RES		0x01
#define CMD_LT_LTD		0x03
#define CMD_LT_RES1		0x04
#define CMD_LT_SSP		0x05
#define CMD_LT_DT		0x06
#define CMD_LT_UAR		0x07
#define CMD_LT_AR		0x08
#define CMD_LT_ARL		0x0a
#define CMD_LT_AR0		0x0d
#define CMD_LT_DC		0x0f

#define	CMD_NT_TIMING		0x00
#define	CMD_NT_RESET		0x01
#define CMD_NT_DU		0x03
#define CMD_NT_EI1		0x04
#define CMD_NT_SSP		0x05
#define CMD_NT_DT		0x06
#define CMD_NT_AR		0x08
#define CMD_NT_ARL		0x0a
#define CMD_NT_AI		0x0c
#define CMD_NT_DI		0x0f

#define TXCI_VAL(cmd,channels)	(((cmd) & XPDS_MCR_TXCI__MASK) | \
					(channels) | \
					XPDS_MCR_TXCI__PMD_ENABLE | \
					XPDS_MCR_TXCI__PMD_RESQ)
/*
 * Indications (read from RXCI in MCR)
 */
#define IND_LT_NULL		(~0)
#define IND_LT_DEAC		0x01
#define IND_LT_FJ		0x02
#define IND_LT_HI		0x03
#define IND_LT_RSY		0x04
#define IND_LT_EI2		0x05
#define IND_LT_INT		0x06
#define IND_LT_UAI		0x07
#define IND_LT_AR		0x08
#define IND_LT_ARM		0x09
#define IND_LT_EI3		0x0b
#define IND_LT_AI		0x0c
#define IND_LT_LSL		0x0d
#define IND_LT_DI		0x0f

#define IND_NT_NULL		(~0)
#define IND_NT_DR		0x00
#define IND_NT_FJ		0x02
#define IND_NT_EI1		0x04
#define IND_NT_INT		0x06
#define IND_NT_PU		0x07
#define IND_NT_AR		0x08
#define IND_NT_ARL		0x0a
#define IND_NT_AI		0x0c
#define IND_NT_AIL		0x0e
#define IND_NT_DC		0x0f

typedef struct {
	int		state;
	u32		rxci;
	int		timer_start_1;
	int		timer_start_2;
	u32		txci;
	int		timer_end;
	int		next_state;
} state_t;

static state_t lt_states[] = {
	{ FSM_LT_RECEIVE_RESET, IND_LT_LSL, -1, -1, CMD_NULL, -1, FSM_LT_AWAKE_ERROR },
	{ FSM_LT_AWAKE_ERROR, IND_LT_AR, 9, -1, CMD_NULL, 9, FSM_LT_EC_TRAINING },
	{ FSM_LT_TEST, IND_LT_DEAC, -1, -1, CMD_LT_DR, -1, FSM_LT_DEACTIVATED },
	{ FSM_LT_DEACTIVATED, IND_LT_DI, -1, -1, CMD_NULL, -1, FSM_LT_ALERTING },
	{ FSM_LT_RESET_FOR_LOOP, IND_LT_DI, 2, -1, CMD_NULL, 2, FSM_LT_ALERTING },
	{ FSM_LT_ALERTING, IND_LT_DI, 1, 2, CMD_LT_AR, 2, FSM_LT_WAIT_FOR_TN },
	{ FSM_LT_WAIT_FOR_TN, IND_LT_DI, -1, -1, CMD_NULL, -1, FSM_LT_AWAKE },
	{ FSM_LT_AWAKE, IND_LT_AR, 1, -1, CMD_LT_AR, -1, FSM_LT_EC_TRAINING },
	{ FSM_LT_EC_TRAINING, IND_LT_AR, 5, -1, CMD_NULL, 5, FSM_LT_EC_CONVERGED },
	{ FSM_LT_EC_CONVERGED, IND_LT_ARM, 6, -1, CMD_LT_ARL, 6, FSM_LT_EQ_TRAINING },
	{ FSM_LT_EQ_TRAINING, IND_LT_ARM, -1, -1, CMD_NULL, -1, FSM_LT_LINE_ACTIVE },
	{ FSM_LT_LINE_ACTIVE, IND_LT_UAI | IND_LT_FJ, -1, -1, CMD_LT_AR0, -1, FSM_LT_PENDING_TRANSPARENT },
	{ FSM_LT_PENDING_TRANSPARENT, IND_LT_UAI | IND_LT_FJ, 8, -1, CMD_NULL, 8, FSM_LT_TRANSPARENT },
	{ FSM_LT_ST_DEACTIVATED, IND_LT_AR | IND_LT_UAI, -1, -1, CMD_LT_AR0, -1, FSM_LT_LINE_ACTIVE },
	{ FSM_LT_LOSS_OF_SIGNAL, IND_LT_LSL, -1, -1, CMD_LT_RES1, -1, FSM_LT_RECEIVE_RESET },
	{ FSM_LT_LOSS_OF_SYNC, IND_LT_RSY, -1, -1, CMD_LT_RES1, -1, FSM_LT_TEAR_DOWN_ERROR },
	{ FSM_LT_TEAR_DOWN_ERROR, IND_LT_EI3 | IND_LT_RSY, -1, -1, CMD_NULL, -1, FSM_LT_RECEIVE_RESET },
	{ FSM_LT_PENDING_DEACTIVATION, IND_LT_DEAC, 10, -1, CMD_NULL, 10, FSM_LT_TEAR_DOWN },
	{ FSM_LT_TEAR_DOWN, IND_LT_DEAC, -1, -1, CMD_NULL, -1, FSM_LT_DEACTIVATED },
};

static state_t nt_states[] = {
	{ FSM_NT_RECEIVE_RESET, IND_NT_DR, 7, -1, CMD_NT_DI, 7, FSM_NT_PENDING_TIMING },
	{ FSM_NT_TEST, IND_NT_DR, -1, -1, CMD_NT_DI, -1, FSM_NT_PENDING_TIMING },
	{ FSM_NT_PENDING_TIMING, IND_NT_DC, 14, -1, CMD_NULL, 14, FSM_NT_DEACTIVATED },
	{ FSM_NT_DEACTIVATED, IND_NT_DC, -1, -1, CMD_NT_TIMING, -1, FSM_NT_IOM_AWAKED },
	{ FSM_NT_IOM_AWAKED, IND_NT_PU, -1, -1, CMD_NULL, -1, FSM_NT_ALERTING },
	{ FSM_NT_ALERTING, IND_NT_DC, 1, 11, CMD_NT_AR, 11, FSM_NT_EC_TRAINING },
	{ FSM_NT_EC_TRAINING, IND_NT_DC, 12, -1, CMD_NULL, 12, FSM_NT_SYNCHRONIZED_1 },
	{ FSM_NT_EQ_TRAINING, IND_NT_DC, -1, -1, CMD_NULL, -1, FSM_NT_WAIT_FOR_SF },
	{ FSM_NT_EQ_TRAINING, IND_NT_DC, -1, -1, CMD_NULL, 1, FSM_NT_PEND_RECEIVE_RES },
	{ FSM_NT_WAIT_FOR_SF, IND_NT_DC, -1, -1, CMD_NULL, -1, FSM_NT_PEND_RECEIVE_RES },
	{ FSM_NT_SYNCHRONIZED_1, IND_NT_AR, -1, -1, CMD_NULL, -1, FSM_NT_SYNCHRONIZED_2 },
	{ FSM_NT_SYNCHRONIZED_2, IND_NT_AR, -1, -1, CMD_NT_AI, -1, FSM_NT_WAIT_FOR_ACT },
	{ FSM_NT_WAIT_FOR_ACT, IND_NT_AR, -1, -1, CMD_NULL, -1, FSM_NT_TRANSPARENT },
	{ FSM_NT_TRANSPARENT, IND_NT_AI, -1, -1, CMD_NULL, -1, FSM_NT_ERROR_ST },
	{ FSM_NT_ERROR_ST, IND_NT_AR, -1, -1, CMD_NULL, -1, FSM_NT_PEND_RECEIVE_RES },
	{ FSM_NT_PEND_RECEIVE_RES, IND_NT_EI1, 13, -1, CMD_NULL, 13, FSM_NT_RECEIVE_RESET },
};

#define LT_NUM_STATES	(sizeof (lt_states) / sizeof (*lt_states))
#define NT_NUM_STATES	(sizeof (nt_states) / sizeof (*nt_states))

static int
get_state_index (int st, int ltnt)
{
	int	i;

	switch (ltnt) {
		case MODE_LT: for (i = 0; i < LT_NUM_STATES; i ++) {
			if (lt_states[i].state == st) return i;
			}
			break;
		case MODE_NT: for (i = 0; i < NT_NUM_STATES; i ++) {
			if (nt_states[i].state == st) return i;
			}
			break;
	}
	dprintk (KERN_ERR "Unknown state %d\n", st);
	return 0;
}

static void
set_timers (int state_index, int ltnt)
{
	switch (ltnt) {
		case MODE_LT: if (lt_states[state_index].timer_start_1 >= 0) {
			timers[lt_states[state_index].timer_start_1] = jiffies;
			}
			if (lt_states[state_index].timer_start_2 >= 0) {
				timers[lt_states[state_index].timer_start_2] = jiffies;
			}
			break;
		case MODE_NT: if (nt_states[state_index].timer_start_1 >= 0) {
			timers[nt_states[state_index].timer_start_1] = jiffies;
			}
			if (nt_states[state_index].timer_start_2 >= 0) {
				timers[nt_states[state_index].timer_start_2] = jiffies;
			}
			break;
	}
}

static void
write_txci (int txci, int channels, int card_num)
{
	u32	val;

	dprintk (KERN_DEBUG "write_txci (0x%x, %d)\n", txci, card_num);
	xpds_read_control_register_quiet (card_num, XPDS_MCR_TXCI,
		&val, XPDS_MAIN);

	val &= ~0xf;
	val |= TXCI_VAL (txci, channels);

	xpds_write_control_register_quiet (card_num, XPDS_MCR_TXCI,
		val, XPDS_MAIN);
}

static void
set_txci (int state_index, int channels, int card_num, int ltnt)
{
	switch (ltnt) {
		case MODE_LT: if (lt_states[state_index].txci != CMD_NULL) {
				write_txci (lt_states[state_index].txci, channels, card_num);
			}
			break;
		case MODE_NT: if (nt_states[state_index].txci != CMD_NULL) {
				write_txci (nt_states[state_index].txci, channels, card_num);
			}
			break;
	}
}

/* lt */
static int
check_state_lt (int state_index, int channels, int card_num)
{
	int	i;
	u32	val;

	if (xpds_data[card_num].rxci_interrupt_received) {
		/* lock against interrupts? */
		xpds_data[card_num].rxci_interrupt_received = 0;
		xpds_read_control_register_quiet (card_num, XPDS_MCR_RXCI,
			&val, XPDS_MAIN);
		val &= XPDS_MCR_RXCI__MASK;
		/* unlock? */
	} else {
		val = IND_LT_NULL;
	}

	if (val != lt_states[state_index].rxci && val != IND_LT_NULL) {
		dprintk (KERN_DEBUG "%s RXCI = 0x%01x, expected 0x%01x\n",
			xpds_devs[card_num].name, val, lt_states[state_index].rxci);
	}

	if (lt_state == FSM_LT_AWAKE) {
		switch (val) {
			case IND_LT_UAI:
				dprintk (KERN_DEBUG "AWAKE/UAI -> PENDING_TRANSPARENT\n");
				return FSM_LT_PENDING_TRANSPARENT;
			case IND_LT_ARM:
				dprintk (KERN_DEBUG "AWAKE/ARM -> EQ_TRAINING\n");
				return FSM_LT_EQ_TRAINING;
			case IND_LT_DI:
				dprintk (KERN_DEBUG "AWAKE/DI -> TEST/RES\n");
				write_txci (CMD_LT_RES, channels, card_num);
				return FSM_LT_TEST;
			case IND_LT_EI3:
				dprintk (KERN_DEBUG "AWAKE/EI3 -> TEST/RES\n");
				write_txci (CMD_LT_RES, channels, card_num);
				return FSM_LT_TEST;
			default:
				write_txci (CMD_LT_AR, channels, card_num);
				return FSM_LT_AWAKE;
		}
	} else if (lt_state == FSM_LT_DEACTIVATED) {
		switch (val) {
			case IND_LT_AR:
				dprintk (KERN_DEBUG "DEACTIVATED/AR -> AWAKE/AR\n");
				write_txci (CMD_LT_AR, channels, card_num);
				return FSM_LT_AWAKE;
			case IND_LT_ARM:
				dprintk (KERN_DEBUG "DEACTIVATED/ARM -> EQ_TRAINING\n");
				return FSM_LT_EQ_TRAINING;
			case IND_LT_EI3:
				dprintk (KERN_DEBUG "DEACTIVATED/EI3 -> TEST/RES\n");
				write_txci (CMD_LT_RES, channels, card_num);
				return FSM_LT_TEST;
			case IND_LT_DI:
				dprintk (KERN_DEBUG "DEACTIVATED/DI -> AWAKE/AR\n");
				write_txci (CMD_LT_AR, channels, card_num);
				return FSM_LT_AWAKE;
			default:
				dprintk (KERN_DEBUG "DEACTIVATED/* -> AWAKE/AR\n");
				write_txci (CMD_LT_AR, channels, card_num);
				return FSM_LT_AWAKE;
		}
	} else if (lt_state == FSM_LT_EC_TRAINING) {
		switch (val) {
			case IND_LT_UAI:
				dprintk (KERN_DEBUG "EC_TRAINING/UAI -> PENDING_TRANSPARENT\n");
				return FSM_LT_PENDING_TRANSPARENT;
			case IND_LT_AI:
				dprintk (KERN_DEBUG "EC_TRAINING/AI -> TRANSPARENT\n");
				return FSM_LT_TRANSPARENT;
			default:
				break;
		}
	} else if (lt_state == FSM_LT_EC_CONVERGED) {
		switch (val) {
			case IND_LT_UAI:
				dprintk (KERN_DEBUG "EC_CONVERGED/UAI -> PENDING_TRANSPARENT\n");
				return FSM_LT_PENDING_TRANSPARENT;
			case IND_LT_AI:
				dprintk (KERN_DEBUG "EC_CONVERGED/AI -> TRANSPARENT\n");
				return FSM_LT_TRANSPARENT;
			default:
				break;
		}
	} else if (lt_state == FSM_LT_EQ_TRAINING) {
		switch (val) {
			case IND_LT_UAI:
				dprintk (KERN_DEBUG "EQ_TRAINING/UAI -> PENDING_TRANSPARENT\n");
				return FSM_LT_PENDING_TRANSPARENT;
			case IND_LT_AI:
				dprintk (KERN_DEBUG "EQ_TRAINING/AI -> TRANSPARENT\n");
				return FSM_LT_TRANSPARENT;
			case IND_LT_EI3:
				dprintk (KERN_DEBUG "EQ_TRAINING/EI3 -> TEST/RES\n");
				write_txci (CMD_LT_RES, channels, card_num);
				return FSM_LT_TEST;
			case IND_LT_DI:
				dprintk (KERN_DEBUG "EQ_TRAINING/DI -> AWAKE/AR\n");
				write_txci (CMD_LT_AR, channels, card_num);
				return FSM_LT_AWAKE;
			default:
				if (val != IND_LT_NULL) dprintk (KERN_DEBUG "EQ_TRAINING/0x%01x\n", val);
				return FSM_LT_EQ_TRAINING;
		}
	} else if (lt_state == FSM_LT_PENDING_TRANSPARENT) {
		switch (val) {
			case IND_LT_RSY:
				dprintk (KERN_DEBUG "PENDING_TRANSPARENT/RSY -> WAIT_FOR_TN/RES1\n");
				write_txci (CMD_LT_RES1, channels, card_num);
				return FSM_LT_WAIT_FOR_TN;
			case IND_LT_AI:
				dprintk (KERN_DEBUG "PENDING_TRANSPARENT/AI -> TRANSPARENT\n");
				return FSM_LT_TRANSPARENT;
			case IND_LT_LSL:
				dprintk (KERN_DEBUG "PENDING_TRANSPARENT/LSL -> RECEIVE_RESET/RES1\n");
				write_txci (CMD_LT_RES1, channels, card_num);
				return FSM_LT_RECEIVE_RESET;
			default:
				return FSM_LT_PENDING_TRANSPARENT;
		}
	} else if (lt_state == FSM_LT_RECEIVE_RESET) {
		switch (val) {
			case IND_LT_AR:
				dprintk (KERN_DEBUG "RECEIVE_RESET/AR -> AWAKE\n");
				return FSM_LT_AWAKE;
			case IND_LT_DI:
				dprintk (KERN_DEBUG "RECEIVE_RESET/DI -> DEACTIVATED/AR\n");
				write_txci (CMD_LT_AR, channels, card_num);
				return FSM_LT_DEACTIVATED;
			default:
				break;
		}
	} else if (lt_state == FSM_LT_TEAR_DOWN_ERROR) {
		switch (val) {
			case IND_LT_LSL:
				dprintk (KERN_DEBUG "TEAR_DOWN_ERROR/LSL -> RECEIVE_RESET\n");
				return FSM_LT_RECEIVE_RESET;
			default:
				break;
		}
	} else if (lt_state == FSM_LT_TEST) {
		switch (val) {
			case IND_LT_DEAC:
				dprintk (KERN_DEBUG "TEST/DEAC -> DEACTIVATED/DR\n");
				write_txci (CMD_LT_DR, channels, card_num);
				return FSM_LT_DEACTIVATED;
			case IND_LT_DI:
				dprintk (KERN_DEBUG "TEST/DI -> DEACTIVATED/DC\n");
				write_txci (CMD_LT_DC, channels, card_num);
				return FSM_LT_DEACTIVATED;
			default:
				break;
		}
	} else if (lt_state == FSM_LT_TRANSPARENT) {
		switch (val) {
			case IND_LT_RSY:
				dprintk (KERN_DEBUG "TRANSPARENT/RSY -> TEAR_DOWN_ERROR/RES1\n");
				write_txci (CMD_LT_RES1, channels, card_num);
				return FSM_LT_TEAR_DOWN_ERROR;
			case IND_LT_LSL:
				dprintk (KERN_DEBUG "TRANSPARENT/LSL -> RECEIVE_RESET\n");
				return FSM_LT_RECEIVE_RESET;
			default:
				break;
		}
	} else if (lt_state == FSM_LT_WAIT_FOR_TN) {
		switch (val) {
			case IND_LT_AR:
				dprintk (KERN_DEBUG "WAIT_FOR_TN/AR -> AWAKE/AR\n");
				write_txci (CMD_LT_AR, channels, card_num);
				return FSM_LT_AWAKE;
			case IND_LT_EI3:
				dprintk (KERN_DEBUG "WAIT_FOR_TN/EI3 -> TEST/RES\n");
				write_txci (CMD_LT_RES, channels, card_num);
				return FSM_LT_TEST;
			case IND_LT_DI:
				dprintk (KERN_DEBUG "WAIT_FOR_TN/DI -> TEST/RES\n");
				write_txci (CMD_LT_RES, channels, card_num);
				return FSM_LT_TEST;
			default:
				break;
		}
	}

	for (i = state_index;
		lt_states[i].state == lt_states[state_index].state; i ++) {
		int	timer_done = 0;

		if (lt_states[i].timer_end < 0) {
			timer_done = 1;
		} else if (jiffies - timers[lt_states[i].timer_end] >=
			timer_limits[lt_states[i].timer_end]) {
			dprintk (KERN_DEBUG "%s timer %d expired\n",
				xpds_devs[card_num].name, lt_states[i].timer_end);
			timer_done = 1;
		} 

		if (timer_done) return lt_states[i].next_state;
	}
	return lt_states[state_index].state;
}

/* nt */
static int
check_state_nt (int state_index, int channels, int card_num)
{
	int	i;
	u32	val;

	if (xpds_data[card_num].rxci_interrupt_received) {
		/* lock ? */
		xpds_data[card_num].rxci_interrupt_received = 0;
		xpds_read_control_register_quiet (card_num, XPDS_MCR_RXCI,
			&val, XPDS_MAIN);
		val &= XPDS_MCR_RXCI__MASK;
		/* unlock ? */
	} else {
		val = IND_NT_NULL;
	}

	if (val != nt_states[state_index].rxci && val != IND_NT_NULL) {
		dprintk (KERN_DEBUG "%s RXCI = 0x%01x, expected 0x%01x\n",
			xpds_devs[card_num].name, val, nt_states[state_index].rxci);
	}

	if (nt_state == FSM_NT_DEACTIVATED) {
		switch (val) {
			case IND_NT_AR:
				dprintk (KERN_DEBUG "DEACTIVATED/AR -> SYNCHRONIZED_2/AI\n");
				write_txci (CMD_NT_AI, channels, card_num);
				return FSM_NT_SYNCHRONIZED_2;
			case IND_NT_PU:
				dprintk (KERN_DEBUG "DEACTIVATED/PU -> IOM_AWAKED/TIMING\n");
				write_txci (CMD_NT_TIMING, channels, card_num);
				return FSM_NT_IOM_AWAKED;
			default:
				return FSM_NT_DEACTIVATED;
		}
	} else if (nt_state == FSM_NT_IOM_AWAKED) {
		switch (val) {
			case IND_NT_DC:
				dprintk (KERN_DEBUG "IOM_AWAKED/DC -> ALERTING/AR\n");
				write_txci (CMD_NT_AR, channels, card_num);
				return FSM_NT_ALERTING;
			default:
				dprintk (KERN_DEBUG "IOM_AWAKED/* -> ALERTING/AR\n");
				write_txci (CMD_NT_AR, channels, card_num);
				return FSM_NT_IOM_AWAKED;
		}
	} else if (nt_state == FSM_NT_ALERTING) {
		switch (val) {
			case IND_NT_AR:
				dprintk (KERN_DEBUG "ALERTING/AR -> SYNCHRONIZED_2/AI\n");
				write_txci (CMD_NT_AI, channels, card_num);
				return FSM_NT_SYNCHRONIZED_2;
			default:
				return FSM_NT_ALERTING;
		}
	} else if (nt_state == FSM_NT_EC_TRAINING) {
		if (val == IND_NT_AR) {
			dprintk (KERN_DEBUG "EC_TRAINING/AR -> WAIT_FOR_ACT/AI\n");
			write_txci (CMD_NT_AI, channels, card_num);
			return FSM_NT_WAIT_FOR_ACT;
		}
	} else if (nt_state == FSM_NT_SYNCHRONIZED_1) {
		if (val == IND_NT_AR) {
			dprintk (KERN_DEBUG "SYNCHRONIZED_1/AR -> SYNCHRONIZED_2/AI\n");
			write_txci (CMD_NT_AI, channels, card_num);
			return FSM_NT_SYNCHRONIZED_2;
		}
	} else if (nt_state == FSM_NT_SYNCHRONIZED_2) {
		switch (val) {
			case IND_NT_AR:
				dprintk (KERN_DEBUG "SYNCHRONIZED_2/AR -> WAIT_FOR_ACT/AI\n");
				write_txci (CMD_NT_AI, channels, card_num);
				return FSM_NT_WAIT_FOR_ACT;
			case IND_NT_AI:
				dprintk (KERN_DEBUG "SYNCHRONIZED_2/AI -> TRANSPARENT\n");
				return FSM_NT_TRANSPARENT;
			case IND_NT_DR:
				dprintk (KERN_DEBUG "SYNCHRONIZED_2/DR -> PEND_DEACT_ST/DI\n");
				write_txci (CMD_NT_DI, channels, card_num);
				return FSM_NT_PEND_DEACT_ST;
			default:
				if (val != IND_NT_NULL) dprintk (KERN_DEBUG "SYNCHRONIZED_2/0x%01x\n", val);
				return FSM_NT_SYNCHRONIZED_2;
		}
	} else if (nt_state == FSM_NT_WAIT_FOR_ACT) {
		switch (val) {
			case IND_NT_AI:
				dprintk (KERN_DEBUG "WAIT_FOR_ACT/AI -> TRANSPARENT\n");
				return FSM_NT_TRANSPARENT;
			case IND_NT_DR:
				write_txci (CMD_NT_DI, channels, card_num);
				dprintk (KERN_DEBUG "WAIT_FOR_ACT/DI -> PEND_DEACT_ST\n");
				return FSM_NT_PEND_DEACT_ST;
			case IND_NT_AR:
				dprintk (KERN_DEBUG "WAIT_FOR_ACT/AR -> WAIT_FOR_ACT/AI\n");
				write_txci (CMD_NT_AI, channels, card_num);
				return FSM_NT_WAIT_FOR_ACT;
			default:
				/* write_txci (CMD_NT_AI, channels, card_num); */
				return FSM_NT_WAIT_FOR_ACT;
		}
	} else if (nt_state == FSM_NT_TRANSPARENT) {
		switch (val) {
			case IND_NT_AR:
				dprintk (KERN_DEBUG "TRANSPARENT/AR -> ERROR_ST\n");
				return FSM_NT_ERROR_ST;
			case IND_NT_DR:
				write_txci (CMD_NT_DI, channels, card_num);
				dprintk (KERN_DEBUG "TRANSPARENT/DR -> PEND_DEACT_ST\n");
				return FSM_NT_PEND_DEACT_ST;
			default:
				return FSM_NT_TRANSPARENT;
		}
	}

	if (val == IND_NT_AR) {
		dprintk (KERN_DEBUG "*/AR -> SYNCHRONIZED_2/AI\n");
		write_txci (CMD_NT_AI, channels, card_num);
		return FSM_NT_SYNCHRONIZED_2;
	}

	for (i = state_index;
		nt_states[i].state == nt_states[state_index].state; i ++) {
		int	timer_done = 0;

		if (nt_states[i].timer_end < 0) {
			timer_done = 1;
		} else if (jiffies - timers[nt_states[i].timer_end] >=
			timer_limits[nt_states[i].timer_end]) {
			dprintk (KERN_DEBUG "%s timer %d expired\n",
				xpds_devs[card_num].name, nt_states[i].timer_end);
			timer_done = 1;
		} 

		if (timer_done) return nt_states[i].next_state;
	}
	return nt_states[state_index].state;
}

/* lt */
int
xpds_fsm_lt (int card_num, int channels, int guard_time)
{
	u32	guard;

	dprintk (KERN_DEBUG "xpds_fsm_lt (%d) (LT - line termination)\n", card_num);

	guard = jiffies + guard_time * HZ;

	/*
	 * Reset
	 */
	xpds_data[card_num].rxci_interrupt_received = 0;
	xpds_write_control_register (card_num, XPDS_MCR_TXCI,
		TXCI_VAL (CMD_LT_RES, channels), XPDS_MAIN);
	/*
	 * Unreset
	 */
	xpds_write_control_register (card_num, XPDS_MCR_TXCI,
		TXCI_VAL (CMD_LT_DR, channels), XPDS_MAIN);

	lt_state = FSM_LT_TEST;

	dprintk (KERN_DEBUG "waiting for RXCI interrupt\n");
	while (! xpds_data[card_num].rxci_interrupt_received &&
		jiffies < guard) {
		schedule ();
	}

	guard = jiffies + guard_time * HZ;

	/*
	 * For each state set the timers, set the TXCI value,
	 * then loop, checking for transition.  We want to
	 * get into the transparent state.
	 */
	while (lt_state != FSM_LT_TRANSPARENT) {
		int	state_index, new_state;

		dprintk (KERN_DEBUG "%s state %d\n", xpds_devs[card_num].name, lt_state);
		state_index = get_state_index (lt_state, MODE_LT);
		set_timers (state_index, MODE_LT);
		set_txci (state_index, channels, card_num, MODE_LT);

		do {
			new_state = check_state_lt (state_index, channels, card_num);
			if (guard_time > 0 && jiffies >= guard) {
				dprintk (KERN_ERR "%s FSM guard timer (%d seconds) expired in state %d\n", xpds_devs[card_num].name, guard_time, lt_state);
				return -ETIME;
			}
			schedule ();
		} while (new_state == lt_state);

		lt_state = new_state;
		guard = jiffies + guard_time * HZ;
	}

	dprintk (KERN_DEBUG "state %d == TRANSPARENT\n", lt_state);

	return 0;
}

/* nt */
int
xpds_fsm_nt (int card_num, int channels, int guard_time)
{
	u32	guard;

	dprintk (KERN_DEBUG "xpds_fsm_nt (%d) (NT - network termination)\n", card_num);

	guard = jiffies + guard_time * HZ;

	/*
	 * Reset
	 */
	xpds_data[card_num].rxci_interrupt_received = 0;
	xpds_write_control_register (card_num, XPDS_MCR_TXCI,
		TXCI_VAL (CMD_NT_RESET, channels), XPDS_MAIN);
	/*
	 * Unreset
	 */
	xpds_write_control_register (card_num, XPDS_MCR_TXCI,
		TXCI_VAL (CMD_NT_DI, channels), XPDS_MAIN);

	nt_state = FSM_NT_TEST;

	dprintk (KERN_DEBUG "waiting for RXCI interrupt\n");
	while ( ! xpds_data[card_num].rxci_interrupt_received &&
		jiffies < guard) {
		schedule ();
	}

	guard = jiffies + guard_time * HZ;

	/*
	 * For each state set the timers, set the TXCI value,
	 * then loop, checking for transition.  We want to
	 * get into the transparent state.
	 */
	while (nt_state != FSM_NT_TRANSPARENT) {
		int	state_index, new_state;

		dprintk (KERN_DEBUG "%s state %d\n", xpds_devs[card_num].name, nt_state);
		state_index = get_state_index (nt_state, MODE_NT);
		set_timers (state_index, MODE_NT);
		set_txci (state_index, channels, card_num, MODE_NT);

		do {
			new_state = check_state_nt (state_index, channels, card_num);
			if (guard_time > 0 && jiffies >= guard) {
				dprintk (KERN_ERR "%s FSM guard timer (%d seconds) expired in state %d\n", xpds_devs[card_num].name, guard_time, nt_state);
				return -ETIME;
			}
			schedule ();
		} while (new_state == nt_state);

		nt_state = new_state;
		guard = jiffies + guard_time * HZ;
	}

	dprintk (KERN_DEBUG "state %d == TRANSPARENT\n", nt_state);

	return 0;
}
