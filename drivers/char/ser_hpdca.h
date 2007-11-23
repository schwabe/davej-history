/*
 * Defines for the 98626/98644/internal serial interface on hp300/hp400
 * (based on the National Semiconductor INS8250/NS16550AF/WD16C552 UARTs)
 *
 * This driver was written by Peter Maydell <pmaydell@chiark.greenend.org.uk>
 * based on informaition gleaned from the NetBSD driver and the ser_ioext
 * driver. Copyright(C) 05/1998.
 *
 * The driver is called hpdca because the NetBSD driver is 'dca' and 
 * I wanted something less generic than hp300...
 *
 * The NetBSD driver works for hp700 as well. I don't have one so I
 * took the easy route and dropped support. However, this might be a good
 * place to start if you're adding it back in again...
 *
 *  N.B. On the hp700 and some hp300s, there is a "secret bit" with
 *  undocumented behavior.  The third bit of the Modem Control Register
 *  (MCR_IEN == 0x08) must be set to enable interrupts.  Failure to do
 *  so can result in deadlock on those machines, whereas there don't seem to
 *  be any harmful side-effects from setting this bit on non-affected
 *  machines.
 */

#ifndef _SER_HPDCA_H_
#define _SER_HPDCA_H_

/* 16bit baud rate divisor (lower byte in dca_data, upper in dca_ier).
 * NB:the hp300 constant is for a clk frequency of 2.4576
 * ie HPDCA_BAUD_BASE = 2457600 / 16
 */
#define HPDCA_BAUD_BASE 153600
#define DCABRD(x) (HPDCA_BAUD_BASE / (x))

/* interface reset/id (300 only) */
#define DCAID0          0x02
#define DCAREMID0       0x82
#define DCAID1          0x42
#define DCAREMID1       0xC2

/* interrupt control (300 only) */
#define DCAIPL(x)       ((((x) >> 4) & 3) + 3)
#define IC_IR           0x40
#define IC_IE           0x80

/* the 16c552 is two 16c550s and a parallel port, so its definitions  
 * of various register bits are OK for us.
 */
#include <linux/16c552.h>

/* This is what the register layout looks like: */
typedef struct 
{
   /* card registers */
   u_char gap0;
   volatile u_char dca_id;    /* (read) */
#define dca_reset dca_id  /* (write) */
   u_char gap1;
   volatile u_char dca_ic;                        /* interrupt control */
   u_char gap2;
   volatile u_char dca_ocbrc;
   u_char gap3;
   volatile u_char dca_lcsm;
   u_char gap4[8];
   /* chip registers */
   struct uart_16c550 uart;
} hpdca_struct;

/* purely because the IOEXT code I'm copying uses 5 :-> */
#define MAX_HPDCA 5

typedef struct 
{
   volatile struct uart_16c550 *uart;
   int hasfifo;                                   /* does this UART have a FIFO? */
   hpdca_struct *board;
   int scode;                                     /* select code of this board */
   int spurious_count;
   int line;
} hpdcaInfoType;

extern int hpdca_num;                             /* number of detected boards */
extern hpdcaInfoType hpdca_info[MAX_HPDCA];

#define curruart(info) ((struct uart_16c550 *)(info->port))

#define ser_DTRon(info)  curruart(info)->MCR |=  DTR
#define ser_RTSon(info)  curruart(info)->MCR |=  RTS
#define ser_DTRoff(info) curruart(info)->MCR &= ~DTR
#define ser_RTSoff(info) curruart(info)->MCR &= ~RTS

#endif /* ndef _SER_HPDCA_H_ */
