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
 *
 *  aac_unix_defs.h
 *
 * Abstract:
 *
 *     Macro definition and typedefs
 *
 --*/

#ifndef _AAC_UNIX_DEFS
#define _AAC_UNIX_DEFS

#define 	AAC_MAX_ADAPTERS	64

#ifndef	TRUE
#define TRUE	1
#define FALSE	0
#endif

typedef unsigned long	AAC_STATUS, *PNT_STATUS;

typedef struct {
	unsigned long	LowPart;
	unsigned long	HighPart;
} LARGE_INTEGER;

typedef LARGE_INTEGER	PHYSICAL_ADDRESS;


typedef struct _AFA_IOCTL_CMD {
	int		cmd;
	intptr_t	arg;
	int		flag;
	cred_t		*cred_p;
	int		*rval_p;
} AFA_IOCTL_CMD, *PAFA_IOCTL_CMD;


//
//  Singly linked list structure. Can be used as either a list head, or
//  as link words.
//

typedef struct _SINGLE_LIST_ENTRY {
    struct _SINGLE_LIST_ENTRY *Next;
} SINGLE_LIST_ENTRY, *PSINGLE_LIST_ENTRY;


//
// Calculate the address of the base of the structure given its type, and an
// address of a field within the structure.
//

#define CONTAINING_RECORD(address, type, field) ((type *)( \
                                                  (char *)(address) - \
                                                  (char *)(&((type *)0)->field)))

typedef	void *	PMDL;
typedef void *	PDEVICE_OBJECT;
typedef void *	PADAPTER_OBJECT;
typedef u32	KIRQL;
typedef void *	HANDLE;
typedef void *	KDPC, *PKDPC;
typedef void *	PFILE_OBJECT;
typedef void *	PIRP;
typedef void *	PDRIVER_OBJECT;
typedef u32	KTIMER;


#define	STATUS_SUCCESS		0x00000000
#define STATUS_PENDING		0x40000001
#define STATUS_IO_TIMEOUT			0xc0000001
#define STATUS_UNSUCCESSFUL			0xc0000002
#define STATUS_INSUFFICIENT_RESOURCES	0xc0000005
#define STATUS_BUFFER_OVERFLOW		0xc0000003


#define OUT



typedef u_int (*PUNIX_INTR_HANDLER)(caddr_t Arg);

//
// Zone Allocation
//

typedef struct _ZONE_SEGMENT_HEADER {
    SINGLE_LIST_ENTRY SegmentList;
    void * Reserved;
} ZONE_SEGMENT_HEADER, *PZONE_SEGMENT_HEADER;

typedef struct _ZONE_HEADER {
    SINGLE_LIST_ENTRY FreeList;
    SINGLE_LIST_ENTRY SegmentList;
    u32 BlockSize;
    u32 TotalSegmentSize;
} ZONE_HEADER, *PZONE_HEADER;


//++
//
// void *
// ExAllocateFromZone(
//     PZONE_HEADER Zone
//     )
//
// Routine Description:
//
//     This routine removes an entry from the zone and returns a pointer to it.
//
// Arguments:
//
//     Zone - Pointer to the zone header controlling the storage from which the
//         entry is to be allocated.
//
// Return Value:
//
//     The function value is a pointer to the storage allocated from the zone.
//
//--

#define ExAllocateFromZone(Zone) \
    (void *)((Zone)->FreeList.Next); \
    if ( (Zone)->FreeList.Next ) (Zone)->FreeList.Next = (Zone)->FreeList.Next->Next

//++
//
// void *
// ExFreeToZone(
//     PZONE_HEADER Zone,
//     void * Block
//     )
//
// Routine Description:
//
//     This routine places the specified block of storage back onto the free
//     list in the specified zone.
//
// Arguments:
//
//     Zone - Pointer to the zone header controlling the storage to which the
//         entry is to be inserted.
//
//     Block - Pointer to the block of storage to be freed back to the zone.
//
// Return Value:
//
//     Pointer to previous block of storage that was at the head of the free
//         list.  NULL implies the zone went from no available free blocks to
//         at least one free block.
//
//--

#define ExFreeToZone(Zone,Block)                                    \
    ( ((PSINGLE_LIST_ENTRY)(Block))->Next = (Zone)->FreeList.Next,  \
      (Zone)->FreeList.Next = ((PSINGLE_LIST_ENTRY)(Block)),        \
      ((PSINGLE_LIST_ENTRY)(Block))->Next                           \
    )

//++
//
// BOOLEAN
// ExIsFullZone(
//     PZONE_HEADER Zone
//     )
//
// Routine Description:
//
//     This routine determines if the specified zone is full or not.  A zone
//     is considered full if the free list is empty.
//
// Arguments:
//
//     Zone - Pointer to the zone header to be tested.
//
// Return Value:
//
//     TRUE if the zone is full and FALSE otherwise.
//
//--

#define ExIsFullZone(Zone) \
    ( (Zone)->FreeList.Next == (PSINGLE_LIST_ENTRY)NULL )


#define RtlCopyMemory( Destination, Source, Size )	bcopy( (Source), (Destination), (Size) )
#define RtlZeroMemory( Destination, Size )			bzero( (Destination), (Size) )

//
//  Doubly-linked list manipulation routines.  Implemented as macros
//  but logically these are procedures.
//

//
//  VOID
//  InitializeListHead(
//      PLIST_ENTRY ListHead
//      );
//

#define InitializeListHead(ListHead) (\
    (ListHead)->Flink = (ListHead)->Blink = (ListHead))

//
//  BOOLEAN
//  IsListEmpty(
//      PLIST_ENTRY ListHead
//      );
//

#define IsListEmpty(ListHead) \
    ((ListHead)->Flink == (ListHead))

//
//  PLIST_ENTRY
//  RemoveHeadList(
//      PLIST_ENTRY ListHead
//      );
//

#define RemoveHeadList(ListHead) \
    (ListHead)->Flink;\
    {RemoveEntryList((ListHead)->Flink)}


//
//  VOID
//  RemoveEntryList(
//      PLIST_ENTRY Entry
//      );
//

#define RemoveEntryList(Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_Flink;\
    _EX_Flink = (Entry)->Flink;\
    _EX_Blink = (Entry)->Blink;\
    _EX_Blink->Flink = _EX_Flink;\
    _EX_Flink->Blink = _EX_Blink;\
    }

//
//  VOID
//  InsertTailList(
//      PLIST_ENTRY ListHead,
//      PLIST_ENTRY Entry
//      );
//

#define InsertTailList(ListHead,Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Blink = _EX_ListHead->Blink;\
    (Entry)->Flink = _EX_ListHead;\
    (Entry)->Blink = _EX_Blink;\
    _EX_Blink->Flink = (Entry);\
    _EX_ListHead->Blink = (Entry);\
    }

#endif /* AAC_UNIX_DEFS */
