/* sis900.h Definitions for SiS ethernet controllers including 7014/7016 and 900 
 * Copyrigth 1999 Silicon Integrated System Corporation
 * References:
 *   SiS 7016 Fast Ethernet PCI Bus 10/100 Mbps LAN Controller with OnNow Support,
 *	preliminary Rev. 1.0 Jan. 14, 1998
 *   SiS 900 Fast Ethernet PCI Bus 10/100 Mbps LAN Single Chip with OnNow Support,
 *	preliminary Rev. 1.0 Nov. 10, 1998
 *   SiS 7014 Single Chip 100BASE-TX/10BASE-T Physical Layer Solution,
 *	preliminary Rev. 1.0 Jan. 18, 1998
 *   http://www.sis.com.tw/support/databook.htm
 */

/* MAC operationl registers of SiS 7016 and SiS 900 ehternet controller */
/* The I/O extent, SiS 900 needs 256 bytes of io address */
#define SIS900_TOTAL_SIZE 0x100

/* Symbolic offsets to registers. */
enum SIS900_registers {
	cr=0x0,                 //Command Register
	cfg=0x4,                //Configuration Register
	mear=0x8,               //EEPROM Access Register
	ptscr=0xc,              //PCI Test Control Register
	isr=0x10,               //Interrupt Status Register
	imr=0x14,               //Interrupt Mask Register
	ier=0x18,               //Interrupt Enable Register
	epar=0x18,              //Enhanced PHY Access Register
	txdp=0x20,              //Transmit Descriptor Pointer Register
        txcfg=0x24,             //Transmit Configuration Register
        rxdp=0x30,              //Receive Descriptor Pointer Register
        rxcfg=0x34,             //Receive Configuration Register
        flctrl=0x38,            //Flow Control Register
        rxlen=0x3c,             //Receive Packet Length Register
        rfcr=0x48,              //Receive Filter Control Register
        rfdr=0x4C,              //Receive Filter Data Register
        pmctrl=0xB0,            //Power Management Control Register
        pmer=0xB4               //Power Management Wake-up Event Register
};

/* Symbolic names for bits in various registers */
enum sis900_command_register_bits {
	RESET   = 0x00000100, SWI = 0x00000080, RxRESET = 0x00000020,
	TxRESET = 0x00000010, RxDIS = 0x00000008, RxENA = 0x00000004,
	TxDIS   = 0x00000002, TxENA = 0x00000001
};

enum sis900_configuration_register_bits {
	DESCRFMT = 0x00000100 /* 7016 specific */, REQALG = 0x00000080,
	SB    = 0x00000040, POW = 0x00000020, EXD = 0x00000010, 
	PESEL = 0x00000008, LPM = 0x00000004, BEM = 0x00000001
};

enum sis900_eeprom_access_reigster_bits {
	MDC  = 0x00000040, MDDIR = 0x00000020, MDIO = 0x00000010, /* 7016 specific */ 
	EECS = 0x00000008, EECLK = 0x00000004, EEDO = 0x00000002,
	EEDI = 0x00000001
};

enum sis900_interrupt_register_bits {
	WKEVT  = 0x10000000, TxPAUSEEND = 0x08000000, TxPAUSE = 0x04000000,
	TxRCMP = 0x02000000, RxRCMP = 0x01000000, DPERR = 0x00800000,
	SSERR  = 0x00400000, RMABT  = 0x00200000, RTABT = 0x00100000,
	RxSOVR = 0x00010000, HIBERR = 0x00008000, SWINT = 0x00001000,
	MIBINT = 0x00000800, TxURN  = 0x00000400, TxIDLE  = 0x00000200,
	TxERR  = 0x00000100, TxDESC = 0x00000080, TxOK  = 0x00000040,
	RxORN  = 0x00000020, RxIDLE = 0x00000010, RxEARLY = 0x00000008,
	RxERR  = 0x00000004, RxDESC = 0x00000002, RxOK  = 0x00000001
};

enum sis900_interrupt_enable_reigster_bits {
	IE = 0x00000001
};

/* maximum dma burst fro transmission and receive*/
#define MAX_DMA_RANGE	7	/* actually 0 means MAXIMUM !! */
#define TxMXDMA_shift   	20
#define RxMXDMA_shift    20
#define TX_DMA_BURST    	0
#define RX_DMA_BURST    	0

/* transmit FIFO threshholds */
#define TX_FILL_THRESH   16
#define TxFILLT_shift   	8
#define TxDRNT_shift    	0
#define TxDRNT_100      (1536>>5)
#define TxDRNT_10		16 

enum sis900_transmit_config_register_bits {
	TxCSI = 0x80000000, TxHBI = 0x40000000, TxMLB = 0x20000000,
	TxATP = 0x10000000, TxIFG = 0x0C000000, TxFILLT = 0x00003F00,
	TxDRNT = 0x0000003F
};

/* recevie FFIFO thresholds */
#define RxDRNT_shift     1
#define RxDRNT_100	8
#define RxDRNT_10		8 

enum sis900_reveive_config_register_bits {
	RxAEP  = 0x80000000, RxARP = 0x40000000, RxATP   = 0x10000000,
	RxAJAB = 0x08000000, RxDRNT = 0x0000007F
};

#define RFAA_shift      28
#define RFADDR_shift    16

enum sis900_receive_filter_control_register_bits {
	RFEN  = 0x80000000, RFAAB = 0x40000000, RFAAM = 0x20000000,
	RFAAP = 0x10000000, RFPromiscuous = (RFAAB|RFAAM|RFAAP)
};

enum sis900_reveive_filter_data_mask {
	RFDAT =  0x0000FFFF
};

/* EEPROM Addresses */
enum sis900_eeprom_address {
	EEPROMSignature = 0x00, EEPROMVendorID = 0x02, EEPROMDeviceID = 0x03,
	EEPROMMACAddr   = 0x08, EEPROMChecksum = 0x0b
};

/* The EEPROM commands include the alway-set leading bit. Refer to NM93Cxx datasheet */
enum sis900_eeprom_command {
	EEread = 0x0180, EEwrite = 0x0140, EEerase = 0x01C0, 
	EEwriteEnable = 0x0130, EEwriteDisable = 0x0100,
	EEeraseAll = 0x0120, EEwriteAll = 0x0110, 
	EEaddrMask = 0x013F, EEcmdShift = 16
};

/* Manamgement Data I/O (mdio) frame */
#define MIIread         0x6000
#define MIIwrite        0x5002
#define MIIpmdShift     7
#define MIIregShift     2
#define MIIcmdLen       16
#define MIIcmdShift     16

/* Buffer Descriptor */
#define OWN             0x80000000
#define MORE            0x40000000
#define INTR            0x20000000
#define OK              0x08000000
#define DSIZE           0x00000FFF

#define SUPCRC          0x10000000
#define ABORT           0x04000000
#define UNDERRUN        0x02000000
#define NOCARRIER       0x01000000
#define DEFERD          0x00800000
#define EXCDEFER        0x00400000
#define OWCOLL          0x00200000
#define EXCCOLL         0x00100000
#define COLCNT          0x000F0000

#define INCCRC          0x10000000
//      ABORT           0x04000000
#define OVERRUN         0x02000000
#define DEST            0x01800000
#define BCAST           0x01800000
#define MCAST           0x01000000
#define UNIMATCH        0x00800000
#define TOOLONG         0x00400000
#define RUNT            0x00200000
#define RXISERR         0x00100000
#define CRCERR          0x00080000
#define FAERR           0x00040000
#define LOOPBK          0x00020000
#define RXCOL           0x00010000

#define RXSTS_shift     18

/* MII register offsets */
#define MII_CONTROL             0x0000
#define MII_STATUS              0x0001
#define MII_PHY_ID0             0x0002
#define MII_PHY_ID1             0x0003
#define MII_ANAR                0x0004
#define MII_ANLPAR              0x0005
#define MII_ANER                0x0006
/* MII Control register bit definitions. */
#define MIICNTL_FDX             0x0100
#define MIICNTL_RST_AUTO        0x0200
#define MIICNTL_ISOLATE         0x0400
#define MIICNTL_PWRDWN          0x0800
#define MIICNTL_AUTO            0x1000
#define MIICNTL_SPEED           0x2000
#define MIICNTL_LPBK            0x4000
#define MIICNTL_RESET           0x8000
/* MII Status register bit significance. */
#define MIISTAT_EXT             0x0001
#define MIISTAT_JAB             0x0002
#define MIISTAT_LINK            0x0004
#define MIISTAT_CAN_AUTO        0x0008
#define MIISTAT_FAULT           0x0010
#define MIISTAT_AUTO_DONE       0x0020
#define MIISTAT_CAN_T           0x0800
#define MIISTAT_CAN_T_FDX       0x1000
#define MIISTAT_CAN_TX          0x2000
#define MIISTAT_CAN_TX_FDX      0x4000
#define MIISTAT_CAN_T4          0x8000
/* MII NWAY Register Bits ...
** valid for the ANAR (Auto-Negotiation Advertisement) and
** ANLPAR (Auto-Negotiation Link Partner) registers */
#define MII_NWAY_NODE_SEL       0x001f
#define MII_NWAY_CSMA_CD        0x0001
#define MII_NWAY_T              0x0020
#define MII_NWAY_T_FDX          0x0040
#define MII_NWAY_TX             0x0080
#define MII_NWAY_TX_FDX         0x0100
#define MII_NWAY_T4             0x0200
#define MII_NWAY_RF             0x2000
#define MII_NWAY_ACK            0x4000
#define MII_NWAY_NP             0x8000

/* MII Auto-Negotiation Expansion Register Bits */
#define MII_ANER_PDF            0x0010
#define MII_ANER_LP_NP_ABLE     0x0008
#define MII_ANER_NP_ABLE        0x0004
#define MII_ANER_RX_PAGE        0x0002
#define MII_ANER_LP_AN_ABLE     0x0001
#define HALF_DUPLEX                     1
#define FDX_CAPABLE_DUPLEX_UNKNOWN      2
#define FDX_CAPABLE_HALF_SELECTED       3
#define FDX_CAPABLE_FULL_SELECTED       4
#define HW_SPEED_UNCONFIG       0
#define HW_SPEED_10_MBPS        10
#define HW_SPEED_100_MBPS       100
#define HW_SPEED_DEFAULT        (HW_SPEED_10_MBPS)

#define ACCEPT_ALL_PHYS         0x01
#define ACCEPT_ALL_MCASTS       0x02
#define ACCEPT_ALL_BCASTS       0x04
#define ACCEPT_ALL_ERRORS       0x08
#define ACCEPT_CAM_QUALIFIED    0x10
#define MAC_LOOPBACK            0x20

//#define FDX_CAPABLE_FULL_SELECTED     4
#define CRC_SIZE                4
#define MAC_HEADER_SIZE         14

#define TX_BUF_SIZE     1536
#define RX_BUF_SIZE     1536

#define NUM_TX_DESC     16      	/* Number of Tx descriptor registers. */
#define NUM_RX_DESC     8       	/* Number of Rx descriptor registers. */

#define TRUE            1
#define FALSE           0

/* PCI stuff, should be move to pci.h */
#define PCI_DEVICE_ID_SI_900	0x900   
#define PCI_DEVICE_ID_SI_7016	0x7016  

/* ioctl for accessing MII transveiver */
#define SIOCGMIIPHY (SIOCDEVPRIVATE)		/* Get the PHY in use. */
#define SIOCGMIIREG (SIOCDEVPRIVATE+1)		/* Read a PHY register. */
#define SIOCSMIIREG (SIOCDEVPRIVATE+2)		/* Write a PHY register */
