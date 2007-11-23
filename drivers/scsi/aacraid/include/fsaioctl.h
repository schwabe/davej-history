/*++
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *   fsaioctl.h
 *
 * Abstract: Defines the interface structures between user mode applications
 *           and the fsa driver.  This structures are used in 
 *           DeviceIoControl() calls.
 *
 *
 *
 --*/
#ifndef _FSAIOCTL_H_
#define _FSAIOCTL_H_

#ifndef IOTRACEUSER

#ifndef CTL_CODE

#define FILE_DEVICE_CONTROLLER          0x00000004

//
// Macro definition for defining IOCTL and FSCTL function control codes.  Note
// that function codes 0-2047 are reserved for Microsoft Corporation, and
// 2048-4095 are reserved for customers.
//

#define CTL_CODE( DeviceType, Function, Method, Access ) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

//
// Define the method codes for how buffers are passed for I/O and FS controls
//

#define METHOD_BUFFERED                 0
#define METHOD_NEITHER                  3

//
// Define the access check value for any access
//
//
// The FILE_READ_ACCESS and FILE_WRITE_ACCESS constants are also defined in
// ntioapi.h as FILE_READ_DATA and FILE_WRITE_DATA. The values for these
// constants *MUST* always be in sync.
//

#define FILE_ANY_ACCESS                 0

#endif

typedef struct _UNIX_QUERY_DISK {
	AAC_INT32	ContainerNumber;
	AAC_INT32	Bus;
	AAC_INT32	Target;
	AAC_INT32	Lun;
	AAC_BOOLEAN	Valid;
	AAC_BOOLEAN	Locked;
	AAC_BOOLEAN	Deleted;
	AAC_INT32	Instance;
	AAC_INT8	diskDeviceName[10];
	AAC_BOOLEAN UnMapped;
} UNIX_QUERY_DISK;
typedef UNIX_QUERY_DISK *PUNIX_QUERY_DISK;

typedef struct _DELETE_DISK {
	AAC_UINT32	NtDiskNumber;
	AAC_UINT32	ContainerNumber;
} DELETE_DISK;
typedef DELETE_DISK *PDELETE_DISK;

#endif /*IOTRACEUSER*/

#define FSACTL_NULL_IO_TEST             0x43    // CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 2048, METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSACTL_SIM_IO_TEST              0x53    // CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 2049, METHOD_NEITHER, FILE_ANY_ACCESS)

#define FSACTL_SENDFIB                  CTL_CODE(FILE_DEVICE_CONTROLLER, 2050, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define FSACTL_GET_VAR			0x93
#define FSACTL_SET_VAR			0xa3
#define FSACTL_GET_FIBTIMES		0xb3
#define FSACTL_ZERO_FIBTIMES		0xc3
#define FSACTL_DELETE_DISK		0x163
#define FSACTL_QUERY_DISK		0x173

// AfaComm perfmon ioctls
#define FSACTL_GET_COMM_PERF_DATA	CTL_CODE(FILE_DEVICE_CONTROLLER, 2084, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_OPENCLS_COMM_PERF_DATA	CTL_CODE(FILE_DEVICE_CONTROLLER, 2085, METHOD_BUFFERED, FILE_ANY_ACCESS)


typedef struct _GET_ADAPTER_FIB_IOCTL {
	char	*AdapterFibContext;
	int	  	Wait;
	char	*AifFib;
} GET_ADAPTER_FIB_IOCTL, *PGET_ADAPTER_FIB_IOCTL;

//
// filesystem ioctls
//
#define FSACTL_OPEN_GET_ADAPTER_FIB		CTL_CODE(FILE_DEVICE_CONTROLLER, 2100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_GET_NEXT_ADAPTER_FIB		CTL_CODE(FILE_DEVICE_CONTROLLER, 2101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_CLOSE_GET_ADAPTER_FIB		CTL_CODE(FILE_DEVICE_CONTROLLER, 2102, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_OPEN_ADAPTER_CONFIG		CTL_CODE(FILE_DEVICE_CONTROLLER, 2103, METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSACTL_CLOSE_ADAPTER_CONFIG		CTL_CODE(FILE_DEVICE_CONTROLLER, 2104, METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSACTL_MINIPORT_REV_CHECK		CTL_CODE(FILE_DEVICE_CONTROLLER, 2107, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_QUERY_ADAPTER_CONFIG		CTL_CODE(FILE_DEVICE_CONTROLLER, 2113, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSACTL_FORCE_DELETE_DISK		CTL_CODE(FILE_DEVICE_CONTROLLER, 2120, METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSACTL_AIF_THREAD			CTL_CODE(FILE_DEVICE_CONTROLLER, 2127, METHOD_NEITHER, FILE_ANY_ACCESS)

#endif // _FSAIOCTL_H_


