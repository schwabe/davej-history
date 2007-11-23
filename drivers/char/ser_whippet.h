/*
 * Defines for the Hisoft Whippet serial port for the Amiga 600/1200
 * range of computers.
 *
 * This code is (C) 1997,98 Chris Sumner (chris@cpsumner.freeserve.co.uk),
 * based on 16c552.h and ser_ioext.h which are (C) 1995 Jes Sorensen.
 * (jds@kom.auc.dk)
 */

#ifndef _SER_WHIPPET_H_
#define _SER_WHIPPET_H_

#define UART_CLK             7372800
#define WHIPPET_BAUD_BASE   (UART_CLK / 16)

#define WHIPPET_PHYSADDR    (0xA30600)	/* from whippet.device */

struct WHIPPET {
	u_char  RBR;		/* Reciever Buffer Register */
	u_char  pad0[0xfff];
	u_char  IER;		/* Interrupt Enable Register */
	u_char  pad1[0xfff];
	u_char  IIR;		/* Interrupt Identification Register */
	u_char  pad2[0xfff];
	u_char  LCR;		/* Line Control Register */
	u_char  pad3[0xfff];
	u_char  MCR;		/* Modem Control Register */
	u_char  pad4[0xfff];
	u_char  LSR;		/* Line Status Register */
	u_char  pad5[0xfff];
	u_char  MSR;		/* Modem Status Register */
	u_char  pad6[0xfff];
	u_char  SCR;		/* Scratch Register */
};

#define THR RBR   /* Transmitter Holding Register */
#define FCR IIR   /* FIFO Control Register */
#define DLL RBR   /* Divisor Latch - LSB */
#define DLM IER   /* Divisor Latch - MSB */

/*
 * Bit-defines for the various registers.
 */

/* IER - Interrupt Enable Register */

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

#define IRQ_RLS   (IRQ_ID1 | IRQ_ID2)
#define IRQ_RDA    IRQ_ID2
#define IRQ_CTI   (IRQ_ID2 | IRQ_ID3)
#define IRQ_THRE   IRQ_ID1
#define IRQ_MS     0

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
#define FIFO_TRIG_14 (RCVR_TRIG_LSB | RCVR_TRIG_MSB)

#define FIFO_SIZE     16

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

#endif
