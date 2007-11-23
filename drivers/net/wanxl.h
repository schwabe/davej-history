/* wanxl.h
 * -*- linux-c -*-
 * 
 * SBE wanXL device driver
 * Copyright (C) 1999 RG Studio s.c., http://www.rgstudio.com.pl/
 * Written by Krzysztof Halasa <khc@rgstudio.com.pl>
 *
 * Portions (C) SBE Inc., used by permission.
 *
 * Sources:
 *	wanXL technical reference manuals
 *	wanXL UNIXware X.25 driver
 *	Donald Becker's skeleton.c driver
 *	"Linux Kernel Module Programming" by Ori Pomerantz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __WANXL_H
#define __WANXL_H

#include <linux/hdlc.h>

#define SPIDER "we use Frame Relay"

#define PDM_OFFSET 0x1000
#define MAX_PDM_LEN 128000
#define MAX_PORTS_PER_CARD 4
#define MAX_IOCTL_PACKET 256

typedef struct {
	u8 magic[2];
	u8 cbText[4];
	u8 cbData[4];
	u8 cbBss[4];
	u8 syms[4];
	u8 cbStack[4];
	u8 entryPoint[4];
	u8 flag[2];
	u8 dstart[4];
	u8 bstart[4];
}PCI360Header;			/* 36 = 0x24 bytes */


#ifdef __KERNEL__

#define WANXL_RX_BUFFERS 10
#define WANXL_TX_BUFFERS 10

typedef struct {
	volatile u32 pd_flags;	/* Control and attribute flags */
	u32 p_data;		/* Physical Unbiased data buffer */
	volatile u32 pd_length;	/* Length of valid data in the buffer */
	u32 pd_max_len;		/* Maximum length of data buffer */

#ifdef SPIDER
	union 	{
		struct {
			volatile u32 dlci; /* Frame Relay DLCI */
			volatile u32 protid; /* Frame Relay encapsulation */
		}fr;
		volatile u32 lci; /* X25 logical channel identifier */
	}mux;
#endif
  
	/*
	 * Anything below this line is not within the PDM, except for
	 * diagnostics. If you change this structure be sure to update
	 * PKT_DESC_SIZE (for GET_LOCAL_PD)
	 */
	u32 pd_timestamp;	/* Used in packet sequencing */
	void *v_data;		/* Logical biased PCI data buffer */
	u32 pd_packet;		/* For use by host as Packet back pointer */
	u32 pd_handle;		/* Host handle used for completion msg */
}pkt_desc;



typedef struct {
	volatile u32 r_flags;	/* Ring Flags */
	u32 ndesc;		/* Number of desciptors */
	u32 pdm_offset;		/* Offset used by PDM to index p_ring */ 
	u32 next_node;		/* Next ring head for this CPU */
	u32 p_inject;		/* Asyncronous packet injector PCI address */
	u32 p_ring;		/* Anchor pointer to 1st desciptor */

	/*
	 * Anything below this line is not within the PDM, except for
	 * diagnostics. If you change this structure be sure to update
	 * DESC_RNG_SIZE (for GET_LOCAL_RP)
	 */

	u32 host_offset;	/* Offset used by host to index p_ring */
	u32 host_free;		/* Source ring reclaim offset. Owned by src */
	pkt_desc *v_inject;	/* Virtual address for injector */
	pkt_desc *v_ring;	/* Virtual address for descriptor ring */
	u32 cpu;		/* CPU servicing this ring [0-32] */
	struct desc_ring *next_cpu; /* Next ring head for this node */
	u32 pNode;		/* Back pointer to to adapter Node structure */
	u32 WT_Handle;		/* Pad reserved for host use */
	u32 WT_State;		/* Pad reserved for host use */
	u32 PWT_Event;		/* Pad reserved for host use */
}desc_ring;




typedef struct {
	volatile u32 valid;	/* Info Structure Valid Flag */

	u32 host_cls;		/* Host's cache line size */
	u32 host_mpm;		/* Multi-Processor Mode Enable */
	u32 pad;		/* Padding for the sake of alignment */
	u32 psd_data[4];	/* Private segment data ptrs for each of
				   the virtual boards (adapters, whatever) */
	/*
	 * Board ID Information
	 */
	u32 port_info[16];	/* Status (I/F) for each num_port */
	u32 num_ports;		/* Maximum number of ports supported by HW */
	u32 mem_base;		/* Physical PCI base address of memory */
	u32 mem_size;		/* Total size of physical on board memory */
	u32 hw_options;		/* Hardware options byte from EEPROM */
	u32 serial_num;		/* Board Serial Number from EEPROM */
	u32 fw_version;		/* Ptr to firmware version string (\0 term) */
	u32 pdm_version;	/* Ptr to PDM version string (\0 term) */
	u32 pdm_compiled;	/* Ptr to PDM compilation timestamp (\0 term)*/

	/*
	 * Lower WAN Driver Configuration Parameters
	 */
	u32 max_frame_size;	/* Maximum frame size */
	u32 avg_msg_size;	/* Average message size */
	u32 scc_rx_bd_cnt;	/* SCC RX buffer pool count */
	u32 scc_tx_bd_cnt;	/* SCC TX buffer pool count */
	u32 scc_rx_mblk_sz;	/* SCC RX buffer size */
	u32 scc_rx_prealloc;	/* SCC RX buffer preallocation count */
	u32 scc_rx_limit;
	u32 scc_tx_limit;

	/*
	 * RX Packet Ring Parameter
	 */
	u32 rx_ring_head;	/* Unbiased address of RX head of heads */
	u32 rx_max_pkts;	/* RX fairness count */
	u32 rx_intr_tmo;	/* Keep alive interrupt interval */
	u32 rx_intr_thrsh;	/* Keep alive interrupt pkt count */
	u32 rx_lazy_tmo;	/* Maximum time between polls */
	u32 rx_lazy_thrsh;	/* Number of empty polls before going to lazy*/
	u32 rx_scc_tmo;		/* Flow control recovery timeout, SCC FLOW */
	u32 rx_host_tmo;	/* Flow control recovery timeout, HOST FLOW */

	/*
	 * TX Packet Ring Parameters
	 */
	u32 tx_ring_head;	/* Unbiased address of RX head of heads */
	u32 tx_max_pkts;	/* TX fairness count */
	u32 tx_intr_tmo;	/* Keep alive interrupt interval (50ths/sec) */
	u32 tx_intr_thrsh;	/* Keep alive pkt count interrupt */
	u32 tx_lazy_thrsh;	/* Number of empty polls before going to lazy*/
	u32 tx_lazy_tmo;	/* Lazy mode timeout */
	u32 tx_flow_tmo;	/* Flow control timeout, desciptor allocation*/
	u32 host_pads[4];
}board_cfg;


#define PD_READY  0x10000000
#define PD_DONE   0x20000000
#define PD_NULL   0x40000000
#define PD_ERR    0x80000000
#define PD_LAST   0x00000100
#define PD_INJ    0x00000200
#define PD_PRI    0x00000003
#define PD_LOWPRI 0x00000000	/* 00 = Low Priority */
#define PD_MEDPRI 0x00000001	/* 01 = Med Priority */
#define PD_HIPRI  0x00000002	/* 10 = High Priority */
#define PD_CTLPRI 0x00000003	/* 11 = Control Message */

/*#define PD_FLAGS_TX PD_MEDPRI*/
#define PD_FLAGS_TX 0
#define PD_FLAGS_RX 0
#define PD_FLAGS_INJ (PD_CTLPRI|PD_INJ)

#define BF_IREQ   0x01000000
#define BF_FLUSH  0x02000000
#define BF_FLOW   0x04000000
#define BF_LOCK   0x08000000
#define BF_HALT   0x10000000
#define BF_SCAN_DN 0x80000000
 
#define HF_IREQ   0x00010000
#define HF_ACTIVE 0x00020000
#define HF_FLUSH  0x00100000              
#define HF_SCAN   0x00800000

#define TX_IRQ_BIAS 16		/* 16 bits left for Tx interrupt mark */


typedef struct sbe_port_s {
	hdlc_device hdlc;	/* HDLC device struct - must be first */
	struct sbe_card_s *card;
	struct sbe_port_s *next_port;
	desc_ring *rx_ring;
	desc_ring *tx_ring;
	pkt_desc *next_rx_pkt;
	pkt_desc *next_tx_pkt;
	pkt_desc *next_tx_pkt_done;
	struct wait_queue *tx_inj;
	struct wait_queue *rx_inj;
	int mode;
	u8 node;
}sbe_port;


typedef struct sbe_card_s {
	board_cfg *config;

	u8 irq;
	u8 running;

	u32 wx_plx_phyaddr;	/* PLX register PCIbus base addr */
	plx9060 *wx_plx_viraddr; /* PLX register virtual base addr */
	u32 wx_mem_phyaddr;	/* memory PCIbus base addr */
	u8 *wx_mem_viraddr;	/* memory virtual base addr */
	u8 *wx_mem_viraddr_order; /* order-saving memory virtual base addr */
	desc_ring* rx_ring;
	desc_ring* tx_ring;

	sbe_port *first_port;
	struct sbe_card_s *next_card;
}sbe_card;

#endif /* __KERNEL__ */



/*
 * IOCTL Options for the bulit interface.
 */

#define B_OPEN        ('B'<<24 | 0x000001) /* Open Node           bio_wrap */
#define B_CLOSE       ('B'<<24 | 0x000002) /* Close Node          bio_wrap */
#define B_CNT_CFM     ('B'<<24 | 0x000004) /* Connect Confirm     b_cnt_cfm */
#define B_SETTUNE     ('B'<<24 | 0x000008) /* Set tuning          b_tnioc */
#define B_GETTUNE     ('B'<<24 | 0x000010) /* Get tuning          b_tnioc */
#define B_ZEROSTATS   ('B'<<24 | 0x000020) /* Set stats = zero    b_stioc */
#define B_GETSTATS    ('B'<<24 | 0x000040) /* Get stats           b_stioc */
#define B_MOD_SIG     ('B'<<24 | 0x000080) /* Get/Set Modem Sigs. b_modsig */

#define B_X21_LOOP_EN ('B'<<24 | 0x000100) /* Req. X.21 loop3c    b_x21lb_3c */
#define B_X21_LOOP_DI ('B'<<24 | 0x000200) /* Exit X.21 loop3c    b_x21lb_3c */
#define B_LW_FLUSH    ('B'<<24 | 0x000400) /* Tx or Rx Flush      b_lw_flush */

#define B_MEM_DUMP    ('B'<<24 | 0x000800) /* Dump PDM memory     b_mem_dump */
#define B_TELLUS      ('B'<<24 | 0x001000) /* Tell me on change   b_tellus */
#define B_CONS_RD     ('B'<<24 | 0x002000) /* Console Read        b_cons_rd */
#define B_CONS_WR     ('B'<<24 | 0x004000) /* Console Write       b_cons_wr */
#define B_START       ('B'<<24 | 0x008000) /* Start Node          bio_wrap */
#define B_CSR         ('B'<<24 | 0x010000) /* Update CSR          b_csr */

/*
 * Return status return values 
 */
#define RSI_IOCTL_ACK        0	/* General:    Done */
#define RSE_IOCTL_FAIL       1	/* General:    Ioctl failed */
#define RSE_IOCTL_NOT_AVAIL  2	/* General:    Ioctl implemeted */
#define RSE_BAD_STRUCT_FMAT  3	/* General:    Invalid Structure format */
#define RSE_BAD_STRUCT_SIZE  4	/* General:    Invalid Structure size */
#define RSE_STRUCT_TOO_BIG   5	/* General:    Structure > MAXIO_BLOCK */
#define RSE_IOCTL_INJ_FAIL   6	/* General:    Ioctl failed */
#define RSE_NO_HW_SUPPORT    7	/* General:    No HW for the request */
#define RSE_BAD_NODE         8	/* General:    Invalid or mismatch node */
#define RSE_NODE_NOT_OPENED  9	/* General:    Node has never been opened */
#define RSE_NODE_NOT_CLOSED 10	/* General:    Node has never been opened */
#define RSE_OUT_OF_MEM      11	/* General:    Could not allocate memory */
#define RSE_NO_UPSTREAM     13	/* General:    Could not send upstream */

#define RSE_OPEN_FAIL       20	/* B_OPEN:     General Open Fail */
#define RSE_ALREADY_OPEN    21	/* B_OPEN:     Port Allready Opened */
#define RSE_START_FAIL      22	/* B_OPEN:     Could not start port */
#define RSE_CLOSE_FAIL      23	/* B_CLOSE:    Port Not Opened */
#define RSE_NO_CONNECT      24	/* B_CLOSE:    Port Not Opened */

#define RSE_NO_SUPPORT_SCC  30	/* B_???TUNE:  WAN_scc_opts not supported */
#define RSE_NO_SUPPORT_OPT  31	/* B_???TUNE:  WAN_opts     not supported */
#define RSE_NO_SUPPORT_TUS  32	/* B_???TUNE:  WAN_tell_us  not supported */
#define RSE_NO_SUPPORT_DEF  33	/* B_???TUNE:  WAN_cpdef    not supported */

#define RSE_NO_STATS        34	/* B_???STATS: Stats not available */
#define RSE_NO_SUPPORT      35	/* B_MOD_SIG:  Invalid mask */

#define RSE_X21_FAIL        36	/* B_X21_LOOP: Failed to enter/quit mode */
#define RSE_X21_REJ         37	/* B_X21_LOOP: Allready in x.21 3c mode */
#define RSE_X21_BAD_HW      38	/* B_X21_LOOP: Failed to enter/quit mode */
#define RSE_X21_DTE_ONLY    39	/* B_X21_LOOP: Failed to enter/quit mode */

#define RSE_RX_FLUSH        40	/* B_LW_FLUSH: Error trying to flush Rx */
#define RSE_TX_FLUSH        41	/* B_LW_FLUSH: Error trying to flush Tx */

#define RSE_DUMP_FAIL       42	/* B_MEM_DUMP: Dump Failed */
#define RSE_BUS_ERROR       43	/* B_MEM_DUMP: Range caused a bus error */
#define RSE_BAD_RANGE       44	/* B_MEM_DUMP: Range too big for buffer */
#define RSI_NO_STATUS     0xff	/* General:    Done */

/*
 * Interrupt Request flag.  see send_upstream()
 */
#define IO_INT		     1
#define IO_NO_INT	     0

/*
 * Block Sizes
 */
#define MAXIO_BLOCK (64 * 4)	/* Max ioctl ioctl block size */
#define DEF_DBLK    (MAXIO_BLOCK - sizeof(bio_wrap) - 8) /* dump size */
#define DEF_CBLK    (32 * 4)	/* Console RD/WR size */
#define MAX_INJ_TRY       10	/* Max time to try to feed ioctl to inj */

#define PUT_INJ            0x1	/* OOB put on injecter */
#define PUT_RING	   0x2	/* INB Insert in ring */
#define PUT_INJ_RING       0x3	/* Try OOB 1st INB 2nd */


/*
 * Wrapper for all ioctls
 */
typedef struct {
	u32 type;		/* bulit ioctl type / Name */
	u32 status;		/* Return Status */
	u32 irp;		/* IO Request Packet */
	u32 drv_data;		/* Driver related Data */
}bio_wrap;


/*****************************************************************************/
/*
 * B_SETTUNE & B_GETTUNE Support
 */

/* Legal values for WAN_opts */
#define WOPS_LOOPBACK   0x1
#define WOPS_SPLIT_CH   0x2
#define WOPS_DCE        0x4

/* Legal values for WAN_scc_opts */
/*     Note: Not all options and modes available for all boards */
/*           Don't set anything that you don't understand.
 */
#define B_MODE_MSK      0x0f	/* Mode mask */
#define B_HDLC          0x00	/* Default HDLC Mode */
#define B_UART          0x04	/* UART Mode */
#define B_BYSYNC        0x08	/* Reserved */
#define B_ETH           0x0c	/* Reserved */

#define B_GLITCH_DET    0x10	/* Enable Glitch Detect mode */
#define B_TRX           0x20	/* Transparent Receiver */
#define B_TTX           0x40	/* Transparent Transmitter */
#define B_NO_AUTO_RTS   0x80	/* No Auto RTS Mode */

#define B_CODE_MSK     0x700	/* Encode/Decode mask */
#define B_NRZ          0x000	/* Default NRZ Mode */
#define B_NRZI_MARK    0x100	/* NRZI Mark */
#define B_FM0          0x200	/* FM0 */
#define B_FM1          0x300	/* FM1 (Set TINV) */
#define B_MANCHESTER   0x400	/* Manchester */
#define B_NRZI_SPACE   0x500	/* NRZI space (Set TINV) */
#define B_DMANCHESTER  0x600	/* Differential Manchester */
#define B_C_RES        0x700	/* Reserved */


/* Legal values for WAN_phy_hw */
#define B_NONE           0x0
#define B_EIA_232        0x1	/* Read only for PCI-360 */
#define B_EIA_422        0x2
#define B_EIA_449        0x3
#define B_EIA_530        0x4
#define B_CCITT_X21      0x5
#define B_CCITT_V35      0x6

/* Legal values for WAN_tell_us */
/*     Note: Not all options available for all modes and all boards */
#define B_RXB        0x000001	/* Receive Buffer */
#define B_TXB        0x000002	/* Transmit Buffer */
#define B_BSY        0x000004	/* Busy Condition, Rx frame lost */
#define B_RXF        0x000008	/* Rx frame received */
#define B_TXE        0x000010	/* Tx Error CTS lost or underrun */
#define B_GRA        0x000080	/* Graceful Stop Complete */
#define B_IDL        0x000100	/* Idle Sequence Status Changed */
#define B_FLG        0x000200	/* Flag Status */
#define B_DCC        0x000400	/* DPLL CS Changed */
#define B_GLt        0x000800	/* Glitch on Tx */
#define B_GLr        0x001000	/* Glitch on Rx */
#define B_DCD        0x010000	/* Change on CD */
#define B_CTS        0x020000	/* Change on CTS */
#define B_RTS        0x040000	/* Change on RTS */
#define B_CAB        0x080000	/* Change of Cable */
#define B_CRD        0x100000	/* Console Read */
#define B_CWR        0x200000	/* Console Write */
#define B_CON        0x400000	/* wan_connect    Based on DCD */
#define B_DCO        0x800000	/* wan_disconnect Based on DCD */

/* Legal values for WAN_cptype */
#define WAN_NONE        0	/* No calling procedures        */
#define WAN_X21P        1	/* X21 calling procedures       */
#define WAN_V25bis      2	/* V25bis calling procedures    */
#define WAN_TRAN      128	/* transparent mode - no call prod. */


#define CP_PADSIZE          40

/*
 * Transparent Mode Parameters
 */
typedef struct {
	u32 WAN_cptype;		/* Variant type. (WAN_TRAN)  */
	u32 tp_txidlp;		/* tx idle pattern */
	u32 tp_txsynp;		/* tx synchronize pattern */
	u32 tp_txsynl;		/* tx synchronize pattern bit len */
	u32 tp_txflag;		/* tx flags */
	u32 tp_rxidlp;		/* rx idle pattern */
	u32 tp_rxsynp;		/* rx synchronize pattern */
	u32 tp_rxsynl;		/* rx synchronize pattern bit len */
	u32 tp_rxflag;		/* rx flags */
}WAN_tpx;


typedef struct {
	u32 WAN_opts;		/* WAN loopback */
	u32 WAN_baud;		/* WAN baud rate */
	u32 WAN_maxframe;	/* WAN maximum frame size */
	u32 WAN_interface;	/* WAN physical interface */
	u32 WAN_phy_hw;		/* Phy. Hw 232/449/X.21 etc */
	u32 WAN_scc_opts;	/* SCC/USCC options */
	u32 WAN_tell_us;	/* Tell Up Stream interrupt info */
	u32 cproc;		/* Reserved */
	union {
		u32 WAN_cptype;	/* Variant type. */
		WAN_tpx WAN_tran; /* Transparent mode configuration */
		u8 WAN__pad[CP_PADSIZE]; /* pad - allows for new defs */
	}WAN_cpdef;		/* WAN call procedural definition for
				   hardware interface. */
}wan_tune;


#ifdef SPIDER
/*
 * Tune/stats types
 */
#define WAN_TYPE 1
#define HDLC_TYPE 1
#define FR_TYPE 2
#define FR_PVC_TYPE 3

/*
 * Standards
 */
#define ITU 0
#define ANSI 1
#define OLDANSI 2
#define OGOF 3

/*
 * Conformance
 */
#define NONE 0
#define SPRINT 1

/*
 * Flow styles
 */
#define FECN 0x01
#define BECN 0x02
#define CLLM 0x04

/*
 * Protocol ids
 */
#define PPP_PROTID 0x1
#define IP_PROTID 0x2
#define ARP_PROTID 0x4

/*
 * Frame Relay tuning structure (required for each physical interface)
 * Suitable defaults are { 0, 10, 15, 6, 3, 4, 1600, 56000, 1, 0 }
 */
typedef struct {
	u32 lmidlci;		/* the LMI DLCI */
	u32 T391;		/* link integrity verification polling timer */
	u32 T392;		/* polling verification timer */
	u32 N391;		/* full status polling counter */
	u32 N392;		/* error threshold */
	u32 N393;		/* monitored events count */
	u32 maxframesize;	/* maximum frame size */
	u32 accessrate;		/* access rate */
	u32 standard;		/* LMI standard */
	u32 conform;		/* conformance requirements */
}fr_tune;

/*
 * Frame Relay PVC tuning structure (required for each PVC)
 * Suitable defaults are { ?, ?, ?, ?, 8, 0 }
 */
typedef struct {
	u32 dlci;		/* dlci for this PVC */
	u32 cir;		/* committed information rate */
	u32 Bc;			/* committed burst size */
	u32 Be;			/* excess burst size */
	u32 stepcount;		/* congestion step counter */
	u32 flowstyle;		/* flow style */
	u32 protid;		/* encapsulation */
}fr_pvc_tune;
#endif

typedef struct {
	bio_wrap bio;		/* type, status, irp & drv data */
#ifndef SPIDER
	wan_tune tune;		/* Structure of tuning values */
#else
	union {
		wan_tune wan;	/* Structure of tuning values */
		fr_tune fr;	/* Structure of tuning values */
		fr_pvc_tune fr_pvc; /* Structure of tuning values */
	}tune;
	u32 type;		/* tune type */
#endif
}b_tnioc;


/*****************************************************************************/

/*
 * B_ZEROSTATS & B_GETSTATS Support
 */



typedef struct {
	u32 hc_txgood;		/* Good frames transmitted */
	u32 hc_txurun;		/* Transmit underruns */
	u32 hc_txctslost;	/* Transmit cts lost */
	u32 hc_rxgood;		/* Good frames received */
	u32 hc_rxorun;		/* Receive overruns */
	u32 hc_rxcrc;		/* Receive CRC/Framing errors */
	u32 hc_rxnobuf;		/* Rx frames with no buffer */
	u32 hc_rxnflow;		/* Rx frames with no flow ctl */
	u32 hc_rxoflow;		/* Receive buffer overflows */
	u32 hc_rxabort;		/* Receive aborts */
	u32 hc_rxcnterr;	/* Rx Counter Errors for VCOM34 */
	u32 hc_rxcdlost;	/* Receive cd lost */
	u32 hc_rxalignerr;	/* Receive align error */
	u32 res1;		/* Future Use */
	u32 res2;		/* Future Use */
	u32 res3;		/* Future Use */
}hdlcstats;

#ifdef SPIDER
/*
 * DLCI status
 */
#define DLCI_NOTATNET 0x80
#define DLCI_NEWSENT  0x40
#define DLCI_NEW      0x08
#define DLCI_ACTIVE   0x02

/*
 * Ioctl block for B_GETSTATS or B_ZEROSTATS command
 */

typedef struct {
	u32 ppastat;		/* status of PPA */
	u32 wanstat;		/* WAN status */
	u32 pvccount;		/* number of DLCIs */
	u32 pad1;		/* NOT USED */
	u32 txframes;		/* total number of frames transmitted */
	u32 rxframes;		/* total number of frames received */
	u32 txbytes;		/* total number of bytes transmitted */
	u32 rxbytes;		/* total number of bytes received */
	u32 txlmipolls;		/* number of PVC status enquiries */
	u32 rxfullstat;		/* number of full status frames received */
	u32 rxseqonly;		/* number of keep alive frames received */
	u32 rxasynchs;		/* number of asynchronous frames received */
	u32 rxcllms;		/* number of CLLM messages received */
	u32 lmierrors;		/* number of bad frames received on LMI DLCI */
	u32 lmitimeouts;	/* number of times T392 timer expired */
	u32 rxtoobig;		/* frames received exceeding maximum size */
	u32 rxinvDLCI;		/* number of frames for invalid DLCIs */
	u32 rxunattDLCI;	/* number of frames for unattached DLCIs */
	u32 rxdrops;		/* number of rx buffer allocation failures */
	u32 txinvrq;		/* number of invalid transmission frames */
	u32 rxinvrq;		/* number of invalid frames received */
	u32 wanflows;		/* number of canput fails on the wan */
	u32 LMIwanflows;	/* ... and for frames on the LMI channel */
	u32 txstops;		/* number of congested transmit frames */
	u32 txnobuffs;		/* number of tx buffer allocation failures */
	u32 pad2;		/* NOT USED */
	u32 rxlmipolls;		/* number of received PVC status enquiries */
	u32 pad3;		/* NOT USED */
	u32 txfullstat;		/* number of full status enquiry responses */
	u32 txseqonly;		/* ... else it is another type of response */
	u32 pad4;		/* NOT USED */
	u32 pad5;		/* NOT USED */
}frstats;

typedef struct {
	u32 dlci;		/* dlci for this PVC */
	u32 txframes;		/* total number of frames transmitted */
	u32 rxframes;		/* total number of frames received */
	u32 txbytes;		/* total number of bytes transmitted */
	u32 rxbytes;		/* total number of bytes received */
	u32 txstops;		/* number of congested transmit frames */
	u32 rxstops;		/* failed canputs to upper read queue */
	u32 rxFECNs;		/* FECN bit set on received frame count */
	u32 rxBECNs;		/* BECN bit set on received frame count */
	u32 pad2;		/* NOT USED */
	u32 status;		/* DLCI status flags */
	u32 pad3;		/* NOT USED */
	u32 cirlowat;		/* CIR low water mark */
	u32 cirhiwat;		/* CIR high water mark */
	u32 txde;		/* discard eligibility transmit total */
	u32 rxde;		/* discard eligibility receive total */
	u32 vcstat;		/* general VC status returned by MFE */
	u32 txPDUs;		/* PDUs successfully sent */
	u32 txFragmentedPDUs;	/* Outgoing PDUs fragmented */
	u32 txFragmentErrors;	/* Fragmentation errors */
	u32 txFragments;	/* Fragments successfully sent */
	u32 txDiscards;		/* Outgoing PDUs discarded */
	u32 txBlocked;		/* Transmission flow controlled */
	u32 rxPDUs;		/* PDUs successfully received */
	u32 rxFragmentedPDUs;	/* Fragmented PDUs received */
	u32 rxFragments;	/* Fragments received */
	u32 rxReassemblyMismatch; /* Missing fragment(s) detected */
	u32 rxReassemblyTooBig;	/* Incoming PDUs too large */
	u32 rxDiscards;		/* Incoming PDUs/fragments discarded */
}frpvcstats;
#endif

/*
 * Ioctl block for B_GETSTATS or B_ZEROSTATS command
 */
typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
#ifndef SPIDER
	hdlcstats hdlc_stats;	/* Table of HDLC stats values */
#else
	union {
		hdlcstats hdlc;	/* Table of HDLC stats values */
		frstats fr;	/* Table of FR stats values */
		frpvcstats frpvc; /* Table of FR PVC stats values */
	}stats;
	u32 type;		/* stats type */
#endif
}b_stioc;


/*****************************************************************************/

/*
 * B_MOD_SIG Support
 */

/*
 *      Note:   All Modem signals are NOT supported on all boards.
 *              It is up to the application to know supported signals
 *              and signal direction based on the ports DTE/DCE
 *              configuration.
 */
#define GET_MOD_SIG    0	/* SBE Get Modem Signals [W_MOD_SIG] */
#define SET_MOD_SIG    1	/* SBE Set Modem Signals [W_MOD_SIG] */




/* Bit map for modem_sigs and modem_mask */
#define M_TI    0x0001		/* Test Indicator */
#define M_LL    0x0002		/* Local Loopback */
#define M_MB    0x0004		/* Make Busy */
#define M_RLSD  0x0008		/* Rx Sig. Detect */
#define M_RI    0x0010		/* Ring Indicator */
#define M_DTR   0x0020		/* Data Term Ready */
#define M_DSR   0x0040		/* Data Set Ready */
#define M_SP    0x0080		/* Spare */
#define M_RTS   0x0100		/* Request to Send */
#define M_CTS   0x0200		/* Clear to Send */
#define M_DCD   0x0400		/* Carrier Detect */
#define M_DTX   0x1000		/* Disable TX */
#define M_DRX   0x2000		/* Disable RX */
#define M_DCE   0x8000		/* DCE Port PCI-360 Read Only */

/*
 * PCI-360 Modem masks
 * Note:  - This board can detect PM phy hardware for DTE or DCE operation.
 */
#define M_P360_SIO       (M_LL | M_DTR | M_DSR | M_RTS | M_DRX | M_DTX)
#define M_P360_SET_DTE   M_P360_SIO
#define M_P360_SET_DCE   M_P360_SIO
#define M_P360_GET_DTE   M_P360_SIO | M_DCE | M_RI | M_DSR
#define M_P360_GET_DCE   M_P360_SIO | M_DCE | M_RI



typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 ctrl;		/* GET_MOD_SIG or SET_MOD_SIG */
	u32 modem_sigs;		/* See above M_??? for bit map */
	u32 modem_mask;		/* Modem signals we care about */
}b_modsig;





/*****************************************************************************/
/*
 * B_X21_LOOP_EN & B_X21_LOOP_DI Support
 */
#define X21_LP_NORM     0	/* Use flags 0x0f only   [X25_LOOP_EN] */
#define X21_LP_SYN      1	/* Prefix flags with SYN [X25_LOOP_EN] */

/* Return Values */
#define X21_ACK         0	/* No Errors */
#define X21_MODE_FAIL   1	/* Could not start /exit req mode */
#define X21_IN_3C_LB    2	/* All ready in Loopback 3c mode */
#define X21_BAD_HW      3	/* Wrong HW Type. (should be X.21) */
#define X21_DTE_ONLY    4	/* DCE not supported */



/*
 * X.21 Lopback 3c Mode
 */

typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 flag;		/* X21_LP_NORM or X21_LP_SYN */
}b_x21lb_3c;


/*****************************************************************************/



/*
 * B_LW_FLUSH Support
 */

#define B_LW_RX_FLUSH    1	/* Flush lower wan driver Rx ring */
#define B_LW_TX_FLUSH    2	/* Flush lower wan driver Tx ring */

typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 flag;		/* B_LW_RX_FLUSH and/or B_LW_TX_FLUSH */
}b_lw_flush;



/*****************************************************************************/
/*
 * B_MEM_DUMP Support
 *    Note: Use with care, dumps may cause unrecoverable errors,
 *          if the dump range points to a chip or invalid memory.
 *          The PDM ignores DEF_DBLK. So the driver may customize
 *          this structure.
 *
 *          Care should be taken when getting a dump from across the
 *          bus.  'start_addr' and  'len' should assure the data
 *          starts and ends on a long word boundary.
 *
 *          'start_addr' is relative to the PDM's CPU.
 */



/*
 * dump structure
 */
typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 start_addr;		/* PDM's address to start dump */
	u32 len;		/* length of dump */
	u32 dump[DEF_DBLK/4]; 	/* Must size >= len */
}b_mem_dump;



/*****************************************************************************/
/*
 * B_TELLUS Support
 *    Note: This ioctl provides a real time way to enable or disable
 *          events that could be sent upstream by the PDM.
 *
 *          See WAN_tell_us above for bit def.
 */

typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 WAN_tell_us;	/* Tell Up Stream interrupt info */
	u32 tell_val;		/* Bits corrispond to WAN_tell_us */
}b_tellus;



/*****************************************************************************/
/*
 * B_CSR Support
 */

typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 csr[MAX_PORTS_PER_CARD]; /* CSR Registers */
}b_csr;



/*****************************************************************************/
/*
 * B_CONS_RD Support
 */

typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 len;		/* length of dump */
	u32 buf[DEF_CBLK];	/* Must size >= len */
}b_cons_rd;



/*****************************************************************************/
/*
 * B_CONS_WR Support
 */

typedef struct {
	bio_wrap bio;		/* type, status, irp &drv data */
	u32 len;		/* length of dump */
	char buf[DEF_CBLK];	/* Must size >= len */
}b_cons_wr;

#endif /* __WANXL_H */
