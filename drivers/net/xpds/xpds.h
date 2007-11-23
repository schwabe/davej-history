/*
 * Copyright 1998, 1999, 2000 Xpeed, Inc.
 * xpds-fr.h, $Revision: 1.10 $
 */
#ifndef XPDS_H
#define XPDS_H 1

#include <asm/types.h>
#include "xpds-encap-fr.h"
#include <linux/xpds-ioctl.h>

extern int xpds_max_cards;

#define DEBUG_MAIN	1
#define DEBUG_FSM	2
#define DEBUG_DETAILED	128

extern int	xpds_debug_level;

typedef struct xpds_rxtx_list_t {
	volatile u32		control;
	u32			buffer_bus_addr;
	u32			next_bus_addr;
	u32			unused1;
	volatile u8		*buffer;
	volatile struct xpds_rxtx_list_t	*next;
	volatile struct xpds_rxtx_list_t	*prev;
	u32			offset;
} xpds_rxtx_list_t;

typedef struct {
	struct pci_dev		*pci_dev;
	volatile u32   		*main_control_registers;
	volatile u32   		*rx_fifo_control_registers;
	volatile u32   		*tx_fifo_control_registers;
	volatile u32   		*rx_dma_control_registers;
	volatile u32   		*tx_dma_control_registers;
	volatile u32   		*rx_fifo_data_registers;
	volatile u32   		*tx_fifo_data_registers;
	volatile u32   		*aux_registers;
	volatile void		*rxtx_mem_allocated;
	volatile xpds_rxtx_list_t	*rx_dma_list;
	volatile xpds_rxtx_list_t	*tx_dma_list;
	volatile xpds_rxtx_list_t	*current_rx_dma;
	volatile xpds_rxtx_list_t	*current_tx_dma;
	struct net_device_stats	stats;
	volatile int		rxci_interrupt_received;
	int			physical_up;
	int			physical_retrying;
	int			is_fpga;
	int			has_last_byte_bug;
	int			is_sdsl;
	int			is_lt;
	int			speed_mode;
	short			dlci;
	xpds_serial_data_t	serial_data;
	int			has_rx_dma_burst_bug;
	int			has_tx_dma_burst_bug;
	volatile void		*config_mem_remapped;
	int			has_tx_dma_low_rate_bug;
	int			sdsl_speed;
	int			current_tx_fifo;
	volatile xpds_rxtx_list_t	*current_hw_tx_dma;
	int			current_rx_fifo;
	volatile xpds_rxtx_list_t	*current_hw_rx_dma;
	int			has_rx_dma_low_rate_bug;
	struct frad_local	frad_data;
	int			bridged_ethernet;
	u8			dlci_cr;
	u8			dlci_lmi;
	dlci_lmi_timer_data_t	dlci_lmi_timer_data[CONFIG_DLCI_COUNT];
	struct timer_list	dlci_lmi_timers[CONFIG_DLCI_COUNT];
} xpds_data_t;

extern struct net_device *xpds_devs;
extern xpds_data_t *xpds_data;

extern int xpds_read_control_register_quiet (int xpds_num, int register_number,
	u32 *value, int which);
extern int xpds_write_control_register_quiet (int xpds_num, int register_number,
	u32 value, int which);
extern int xpds_read_control_register (int xpds_num, int register_number,
	u32 *value, int which);
extern int xpds_write_control_register (int xpds_num, int register_number,
	u32 value, int which);

#define XPDS_MAIN    		0
#define XPDS_RX_FIFO		2
#define XPDS_TX_FIFO		3
#define XPDS_RX_DMA		4
#define XPDS_TX_DMA  		5
#define XPDS_RX_FIFO_DATA	6
#define XPDS_TX_FIFO_DATA	7
#define XPDS_AUX		8

#define MAIN            XPDS_MAIN
#define RX_FIFO         XPDS_RX_FIFO
#define TX_FIFO         XPDS_TX_FIFO
#define RX_DMA          XPDS_RX_DMA
#define TX_DMA          XPDS_TX_DMA
#define RX_FIFO_DATA    XPDS_RX_FIFO_DATA
#define TX_FIFO_DATA    XPDS_TX_FIFO_DATA
#define AUX             XPDS_AUX

extern int xpds_tx (u8 *buffer, unsigned int len, struct net_device *dev);

#ifdef __SMP__
#define schedule_if_no_interrupt(card_num) \
	do { \
	} while (0)
#else
#define schedule_if_no_interrupt(card_num) \
	do { \
		if (!in_interrupt()) schedule (); \
	} while (0)
#endif

#define DELAY(n,card_num)	schedule_timeout(HZ*(n))
#define DELAY_HZ(n,card_num)	schedule_timeout(n)

#endif
