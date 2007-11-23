/*
 * Definitions for the 16c552 DACE
 * (dual-asynchronous-communications-element) used on the GVP
 * IO-Extender. 
 *
 * Basically this is two 16c550 uarts's and a parallel port, which is
 * why the serial definitions should be valid for the 16c550 uart
 * aswell.
 *
 * Data was taken from National Semiconductors duart 16c552
 * data-sheets and the Texas Instruments DACE 16c552 data-sheets (the
 * NS version of the chip is _non_ standard and their data-sheets did
 * cost me several wasted hours of work).
 *
 * This file is (C) 1995 Jes Sorensen (jds@kom.auc.dk)
 *
 * Moved from drivers/char/ to include/linux/, because it's useful
 * on more than just the one card. I'm using it on the hp300 DCA
 * serial driver, for example.  
 *      -- Peter Maydell <pmaydell@chiark.greenend.org.uk> 05/1998
 */

#ifndef _16C552_H_
#define _16C552_H_

/* Serial stuff */

struct uart_16c550 {
	volatile u_char skip0;
	volatile u_char RBR;
	volatile u_char skip1;
	volatile u_char IER;
	volatile u_char skip2;
	volatile u_char IIR;
	volatile u_char skip3;
	volatile u_char LCR;
	volatile u_char skip4;
	volatile u_char MCR;
	volatile u_char skip5;
	volatile u_char LSR;
	volatile u_char skip6;
	volatile u_char MSR;
	volatile u_char skip7;
	volatile u_char SCR;
};

#define THR RBR
#define FCR IIR
#define DLL RBR
#define DLM IER
#define AFR IIR

/*
 * Bit-defines for the various registers.
 */


/* IER */

#define ERDAI         (1<<0)
#define ETHREI        (1<<1)
#define ELSI          (1<<2)
#define EMSI          (1<<3)

/* IIR - Interrupt Ident. Register */

#define IRQ_PEND      (1<<0) /* NOTE: IRQ_PEND=0 implies irq pending */
#define IRQ_ID1       (1<<1)
#define IRQ_ID2       (1<<2)
#define IRQ_ID3       (1<<3)
#define FIFO_ENA0     (1<<6) /* Both these are set when FCR(1<<0)=1 */
#define FIFO_ENA1     (1<<7)

#define IRQ_RLS  (IRQ_ID1 | IRQ_ID2)
#define IRQ_RDA  (IRQ_ID2)
#define IRQ_CTI  (IRQ_ID2 | IRQ_ID3)
#define IRQ_THRE (IRQ_ID1)
#define IRQ_MS   0

/* FCR - FIFO Control Register */

#define FIFO_ENA      (1<<0)
#define RCVR_FIFO_RES (1<<1)
#define XMIT_FIFO_RES (1<<2)
#define DMA_MODE_SEL  (1<<3)
#define RCVR_TRIG_LSB (1<<6)
#define RCVR_TRIG_MSB (1<<7)

#define FIFO_TRIG_1   0x00
#define FIFO_TRIG_4   RCVR_TRIG_LSB
#define FIFO_TRIG_8   RCVR_TRIG_MSB
#define FIFO_TRIG_14  RCVR_TRIG_LSB|RCVR_TRIG_MSB

/* LCR - Line Control Register */

#define WLS0          (1<<0)
#define WLS1          (1<<1)
#define STB           (1<<2)
#define PEN           (1<<3)
#define EPS           (1<<4)
#define STICK_PARITY  (1<<5)
#define SET_BREAK     (1<<6)
#define DLAB          (1<<7)

#define data_5bit      0x00
#define data_6bit      0x01
#define data_7bit      0x02
#define data_8bit      0x03


/* MCR - Modem Control Register */

#define DTR           (1<<0)
#define RTS           (1<<1)
#define OUT1          (1<<2)
#define OUT2          (1<<3)
#define LOOP          (1<<4)

/* LSR - Line Status Register */

#define DR            (1<<0)
#define OE            (1<<1)
#define PE            (1<<2)
#define FE            (1<<3)
#define BI            (1<<4)
#define THRE          (1<<5)
#define TEMT          (1<<6)
#define RCVR_FIFO_ERR (1<<7)

/* MSR - Modem Status Register */

#define DCTS          (1<<0)
#define DDSR          (1<<1)
#define TERI          (1<<2)
#define DDCD          (1<<3)
#define CTS           (1<<4)
#define DSR           (1<<5)
#define RING_I        (1<<6)
#define DCD           (1<<7)

/* AFR - Alternate Function Register */

#define CONCUR_WRITE  (1<<0)
#define BAUDOUT       (1<<1)
#define RXRDY         (1<<2)

/* Parallel stuff */

/*
 * Unfortunately National Semiconductors did not supply the
 * specifications for the parallel port in the chip :-(
 * TI succed though, so here they are :-)
 */
struct IOEXT_par {
	volatile u_char skip0;
	volatile u_char DATA;
	volatile u_char skip1;
	volatile u_char STATUS;
	volatile u_char skip2;
	volatile u_char CTRL;
};

/* 
 * bit defines for 16c552 (8255) parallel status port
 */
#define LP_PBUSY	0x80  /* inverted input, active high */
#define LP_PACK		0x40  /* unchanged input, active low */
#define LP_POUTPA	0x20  /* unchanged input, active high */
#define LP_PSELECD	0x10  /* unchanged input, active high */
#define LP_PERRORP	0x08  /* unchanged input, active low */

/* 
 * defines for 16c552 (8255) parallel control port
 */
#define LP_PINTEN	0x10  /* high to read data in or-ed with data out */
#define LP_PSELECP	0x08  /* inverted output, active low */
#define LP_PINITP	0x04  /* unchanged output, active low */
#define LP_PAUTOLF	0x02  /* inverted output, active low */
#define LP_PSTROBE	0x01  /* short high output on raising edge */

#endif
