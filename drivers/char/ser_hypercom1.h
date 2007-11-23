/*
 * Serial driver for VMC's HyperCOM1 Serial Card for the Amiga1200.
 * Copyright Gordon Huby 25-May-1999
 * Email: <gordon@ghuby.freeserve.co.uk>
 * ---------------------------------------------------------------------------
 * Hypercom1 == StarTech 16c650 UART
 * ---------------------------------------------------------------------------
 * $Id: ser_hypercom1.h,v 1.12 1999/07/08 18:51:03 gordon Exp $
 * ---------------------------------------------------------------------------
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 * ---------------------------------------------------------------------------
 */

#ifndef	_SER_HYPERCOM1_H_
#define	_SER_HYPERCOM1_H_

#define	HYPERCOM1_CLOCK_RATE	(7372800)
#define	HYPERCOM1_BAUD_BASE	(HYPERCOM1_CLOCK_RATE/16)
#define	HYPERCOM1_PHYSADDR	(0xD80021)

#define FIFO_SIZE		(32)

#ifndef PAD_S
#define PAD_S			3
#endif

struct STARTECH_16C650 {
	u_char	RHR;		/* Reciever Holding Register */
	u_char	pad0[PAD_S];
	u_char	IER;		/* Interupt Enable Register */
	u_char	pad1[PAD_S];
	u_char	FCR;		/* FIFO control register */
	u_char	pad2[PAD_S];
	u_char	LCR;		/* Line control register */
	u_char	pad3[PAD_S];
	u_char	MCR;		/* Modem Control Register */
	u_char	pad4[PAD_S];
	u_char	LSR;		/* Line Status Register */
	u_char	pad5[PAD_S];
	u_char	MSR;		/* Modem Status Register */
	u_char	pad6[PAD_S];
	u_char	SCR;		/* Scratchpad Register */
};

#define	THR	RHR		/* Transmit Holding Register */
#define	ISR	FCR		/* Interrupt Status Register */

#define	LSB	RHR		/* Divisor latch lower address. Enabled only when bit 7 of MCR = 1 */
#define	MSB	IER		/* Divisor latch middle address. Enabled only when bit 7 of MC1 = 1 */

#define DLL	LSB
#define DLM	MSB

#define EFR	RHR		/* Enhanced Feature Register Set, Enabled only when LCR is set to 0xBF */
#define XON1	IER
#define XON2	FCR
#define XOFF1	LCR
#define XOFF2	MCR

/*
 * BIT DEFS FOR ST 16c650
 */

/* Interrupt Enable Register */		/* From Page 22 of st16c650.pdf */
#define	IER_RHR		(1<<0)		/* Recieve Holding Register */
#define	IER_THR		(1<<1)		/* Transmit Holding Register */
#define	IER_RLS		(1<<2)		/* Recieve Line Status Interrupt */
#define	IER_MSI		(1<<3)		/* Modem Status Interrupt */
#define	IER_SM		(1<<4)		/* Sleep Mode */
#define	IER_XOI		(1<<5)		/* Xoff Interrupt */
#define	IER_RTS		(1<<6)		/* RTS Interrupt */
#define	IER_CTS		(1<<7)		/* CTS Interupt */

/* Fifo control register */
#define	FCR_FE			(1<<0)		/* Fifo Enable */
#define	FCR_RFR			(1<<1)		/* Recieve Fifo reset */
#define	FCR_XFR			(1<<2)		/* XMIT Fifo Reset */
#define	FCR_DMS			(1<<3)		/* DMA Mode Select */
#define	FCR_TX_TRIG_LSB		(1<<4)		/* TX Trigger LSB */
#define	FCR_TX_TRIG_MSB		(1<<5)		/* TX Trigger MSB */
#define	FCR_RX_TRIG_LSB	        (1<<6)		/* RX Trigger LSB */
#define	FCR_RX_TRIG_MSB	        (1<<7)		/* RX Trigger MSB */

#define	TX_FIFO_TRIG_16		(0x00)
#define	TX_FIFO_TRIG_8		(FCR_TX_TRIG_LSB)
#define	TX_FIFO_TRIG_24		(FCR_TX_TRIG_MSB)
#define	TX_FIFO_TRIG_30		(FCR_TX_TRIG_LSB | FCR_TX_TRIG_MSB)

#define	RX_FIFO_TRIG_8	        (0x00)
#define	RX_FIFO_TRIG_16	        (FCR_RX_TRIG_LSB)
#define	RX_FIFO_TRIG_24	        (FCR_RX_TRIG_MSB)
#define	RX_FIFO_TRIG_28	        (FCR_RX_TRIG_LSB | FCR_RX_TRIG_MSB)

/* Interupt Status Register */
#define	ISR_IS		(1<<0)		/* INT status */
#define	ISR_IPB0	(1<<1)		/* INT priority bit 0 */
#define	ISR_IPB1	(1<<2)		/* INT priority bit 1 */
#define	ISR_IPB2	(1<<3)		/* INT prioroty bit 2 */
#define	ISR_IPB3	(1<<4)		/* INT priority bit 3 */
#define	ISR_IPB4	(1<<5)		/* INT priority bit 4 */
#define	ISR_FE1		(1<<6)		/* Fifo's enabled */
#define	ISR_FE2		(1<<7)		/* Fifo's enabled */

#define	IRQ_PEND	ISR_IS		/* Logic 0 = pending int, Logic 1 = no pending int */
#define	IRQ_RLS		(0x06)		/* Reciever line status register */
#define	IRQ_RDR		(0x04)		/* Recieved Data Ready */
#define	IRQ_RDTO	(0x0C)		/* Recieve Data time out */
#define	IRQ_THRE	(0x02)		/* Transmitter Holding Register Empty */
#define	IRQ_MSR		(0x00)		/* Modem Status Register */
#define	IRQ_RXOFF	(0x10)		/* (Recieved Xoff signal) / Special character */
#define	IRQ_CTSRTS	(0x20)		/* CTS, RTS change of state */

#define	IRQ_MASK	(ISR_IPB0 | ISR_IPB1 | ISR_IPB2 | ISR_IPB3 | ISR_IPB4)

/* Line Control Register */
#define	LCR_WLB0	(1<<0)		/* Word Length Bit 0 */
#define	LCR_WLB1	(1<<1)		/* Word Length Bit 1 */
#define	LCR_SB		(1<<2)		/* Stop Bits */
#define	LCR_PE		(1<<3)		/* Parity enable */
#define	LCR_EP		(1<<4)		/* Even Parity */
#define	LCR_SP		(1<<5)		/* Set Parity */
#define	LCR_SETB	(1<<6)		/* Set Break */
#define	LCR_DLE		(1<<7)		/* Divisor Latch Enable */

#define	data_5bit	(0x00)
#define	data_6bit	(0x01)
#define	data_7bit	(0x02)
#define	data_8bit	(0x03)

/* Modem Control Register */
#define	MCR_DTR		(1<<0)		/* DTR */
#define	MCR_RTS		(1<<1)		/* RTS */
#define MCR_OP1		(1<<2)		/* OP1 */
#define MCR_OP2		(1<<3)		/* OP1 / IRQx enable  */
#define MCR_LB		(1<<4)		/* Loop Back */
#define MCR_ITS		(1<<5)		/* INT type select */
#define MCR_IRE		(1<<6)		/* IR enable */
#define MCR_CS		(1<<7)		/* Clock Select. logic 0=normal logic 1=4*baudrate*/

/* Line Status Register */
#define	LSR_RDR		(1<<0)		/* Recieve Data Ready */
#define	LSR_ORE		(1<<1)		/* OverRun Error */
#define	LSR_PE		(1<<2)		/* Parity Error */
#define	LSR_FE		(1<<3)		/* Framing Error */
#define	LSR_BI		(1<<4)		/* Break Interupt */
#define	LSR_THE		(1<<5)		/* Trans Holding Empty */
#define	LSR_TE		(1<<6)		/* Trans Empty */
#define	LSR_FDE		(1<<7)		/* Fifo Data Error */

/* Modem Status Register */
#define	MSR_DCTS	(1<<0)		/* Delta CTS */
#define	MSR_DDSR	(1<<1)		/* Delta DSR */
#define	MSR_DRI		(1<<2)		/* Delta RI */
#define MSR_DCD		(1<<3)		/* Delta CD */
#define MSR_CTS		(1<<4)		/* CTS */
#define MSR_DSR		(1<<5)		/* DSR */
#define MSR_RING	(1<<6)		/* RI */
#define MSR_CD		(1<<7)	        /* CD */

/* Enhanced Feature Register */
#define EFR_CONT0       (1<<0)          /* Cont0 TX RX Control */
#define EFR_CONT1       (1<<1)          /* Cont1 TX RX Control */
#define EFR_CONT2       (1<<2)          /* Cont2 TX RX Control */
#define EFR_CONT3       (1<<3)          /* Cont3 TX RX Control */
#define EFR_ENA         (1<<4)          /* Enable IER bits 4-7, ISR FCR bits 4-5, MCR bits 5-7 */ 
#define EFR_SCS         (1<<5)          /* Special Char Select */
#define EFR_AUTORTS     (1<<6)          /* Auto RTS */
#define EFR_AUTOCTS     (1<<7)          /* Auto CTS */

#endif	/* _SER_HYPERCOM1_H_ */
