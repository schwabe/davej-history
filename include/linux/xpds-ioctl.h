/*
 * Copyright 1999, 2000 Xpeed, Inc.
 * xpds-ioctl.h $Revision: 1.6 $
 * License to copy and distribute is GNU General Public License, version 2.
 */
#ifndef XPDS_IOCTL_H
#define XPDS_IOCTL_H 1

#include <linux/types.h>
#include <linux/sockios.h>

#define XPDS_IOCTL_SET_DLCI		(SIOCDEVPRIVATE + 0)
#define XPDS_IOCTL_SET_DEBUG		(SIOCDEVPRIVATE + 1)
#define XPDS_IOCTL_SET_LT		(SIOCDEVPRIVATE + 2)
#define XPDS_IOCTL_SET_IDSL_MODE	(SIOCDEVPRIVATE + 3)
#define XPDS_IOCTL_SET_MODE		XPDS_IOCTL_SET_IDSL_MODE
#define XPDS_IOCTL_SET_SDSL_SPEED	(SIOCDEVPRIVATE + 4)
#define XPDS_IOCTL_SET_SDSL_INVERT	(SIOCDEVPRIVATE + 5)
#define XPDS_IOCTL_SET_SDSL_SWAP	(SIOCDEVPRIVATE + 6)
#define XPDS_IOCTL_INSTALL_FLASH	(SIOCDEVPRIVATE + 7)
#define XPDS_IOCTL_GET_SDSL_INFO	(SIOCDEVPRIVATE + 8)
#define XPDS_IOCTL_SET_SDSL_INFO	(SIOCDEVPRIVATE + 9)
#define XPDS_IOCTL_GET_SDSL_STATE	(SIOCDEVPRIVATE + 10)
#define XPDS_IOCTL_SET_LOOPBACK		(SIOCDEVPRIVATE + 11)
#define XPDS_IOCTL_SET_SDSL_LOOPBACK	XPDS_IOCTL_SET_LOOPBACK
#define XPDS_IOCTL_SET_ASIC_LOOPBACK	XPDS_IOCTL_SET_LOOPBACK
#define XPDS_IOCTL_SET_BRIDGED_ETHERNET	(SIOCDEVPRIVATE + 13)
#define XPDS_IOCTL_SET_DLCI_CR		(SIOCDEVPRIVATE + 14)
#define XPDS_IOCTL_SET_DLCI_LMI		(SIOCDEVPRIVATE + 15)

/*
 * For XPDS_IOCTL_GET_SDSL_INFO and XPDS_IOCTL_SET_SDSL_INFO.
 */
typedef struct {
	__u16			seprom_revision;
	__u8			hardware_version[2];
	__u8			software_version[2];
	__u8			firmware_version[2];
	__u8			mfg_date[4];
	__u8			mac_address[6];
	__u8			serial_number[16];
} xpds_serial_data_t;

/*
 * For XPDS_IOCTL_SET_LOOPBACK
 */
typedef struct {
	int	loopback_type;
	int	on;
} xpds_loopback_parameters_t;

#define XPDS_ASIC_LOOPBACK	1
#define XPDS_SDSL_LOOPBACK	2

#endif
