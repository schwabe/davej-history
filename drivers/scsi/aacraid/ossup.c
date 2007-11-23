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
 *  ossup.c
 *
 * 
 *
 --*/

#include "osheaders.h"

#include "aac_unix_defs.h"



/*++

Routine Description:

    This function initializes a zone header.  Once successfully
    initialized, blocks can be allocated and freed from the zone, and
    the zone can be extended.

Arguments:

    Zone - Supplies the address of a zone header to be initialized.

    BlockSize - Supplies the block size of the allocatable unit within
                the zone.  The size must be larger that the size of the
                initial segment, and must be 64-bit aligned.

    InitialSegment - Supplies the address of a segment of storage.  The
                     first ZONE_SEGMENT_HEADER-sized portion of the segment
                     is used by the zone allocator.  The remainder of
                     the segment is carved up into fixed size
                     (BlockSize) blocks and is made available for
                     allocation and deallocation from the zone.  The
                     address of the segment must be aligned on a 64-bit
                     boundary.

    InitialSegmentSize - Supplies the size in bytes of the InitialSegment.

Return Value:

    STATUS_UNSUCCESSFUL - BlockSize or InitialSegment was not aligned on
                          64-bit boundaries, or BlockSize was larger than
                          the initial segment size.

    STATUS_SUCCESS - The zone was successfully initialized.

--*/

AAC_STATUS ExInitializeZone(PZONE_HEADER Zone, u32 BlockSize, void * InitialSegment, u32 InitialSegmentSize)
{
    u32 i;
    char * p;


    Zone->BlockSize = BlockSize;

    Zone->SegmentList.Next = &((PZONE_SEGMENT_HEADER) InitialSegment)->SegmentList;
    ((PZONE_SEGMENT_HEADER) InitialSegment)->SegmentList.Next = NULL;
    ((PZONE_SEGMENT_HEADER) InitialSegment)->Reserved = NULL;

    Zone->FreeList.Next = NULL;

    p = (char *)InitialSegment + sizeof(ZONE_SEGMENT_HEADER);

    for (i = sizeof(ZONE_SEGMENT_HEADER);
         i <= InitialSegmentSize - BlockSize;
         i += BlockSize
        ) {
        ((PSINGLE_LIST_ENTRY)p)->Next = Zone->FreeList.Next;
        Zone->FreeList.Next = (PSINGLE_LIST_ENTRY)p;
        p += BlockSize;
    }
    Zone->TotalSegmentSize = i;

    return STATUS_SUCCESS;
}


/*++

Routine Description:

    This function extends a zone by adding another segment's worth of
    blocks to the zone.

Arguments:

    Zone - Supplies the address of a zone header to be extended.

    Segment - Supplies the address of a segment of storage.  The first
              ZONE_SEGMENT_HEADER-sized portion of the segment is used by the
              zone allocator.  The remainder of the segment is carved up
              into fixed-size (BlockSize) blocks and is added to the
              zone.  The address of the segment must be aligned on a 64-
              bit boundary.

    SegmentSize - Supplies the size in bytes of Segment.

Return Value:

    STATUS_UNSUCCESSFUL - BlockSize or Segment was not aligned on
                          64-bit boundaries, or BlockSize was larger than
                          the segment size.

    STATUS_SUCCESS - The zone was successfully extended.

--*/

AAC_STATUS ExExtendZone(PZONE_HEADER Zone, void * Segment, u32 SegmentSize)
{
    u32 i;
    char * p;

    ((PZONE_SEGMENT_HEADER) Segment)->SegmentList.Next = Zone->SegmentList.Next;
    Zone->SegmentList.Next = &((PZONE_SEGMENT_HEADER) Segment)->SegmentList;

    p = (char *)Segment + sizeof(ZONE_SEGMENT_HEADER);

    for (i = sizeof(ZONE_SEGMENT_HEADER); i <= SegmentSize - Zone->BlockSize; i += Zone->BlockSize) 
    {
	((PSINGLE_LIST_ENTRY)p)->Next = Zone->FreeList.Next;
        Zone->FreeList.Next = (PSINGLE_LIST_ENTRY)p;
        p += Zone->BlockSize;
    }
    Zone->TotalSegmentSize += i;

    return STATUS_SUCCESS;
}

/* Function: InqStrCopy()
 *
 * Arguments: [2] pointer to char
 *
 * Purpose: Copy a String from one location to another
 * without copying \0
 */

void InqStrCopy(char *a, char *b)
{
	while(*a != (char)0) 
		*b++ = *a++;
}

