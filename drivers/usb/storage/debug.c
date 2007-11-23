/* Driver for USB Mass Storage compliant devices
 * Debugging Functions Source Code File
 *
 * $Id: debug.c,v 1.1 2000/06/27 01:25:28 mdharm Exp $
 *
 * Current development and maintainance by:
 *   (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "debug.h"

void usb_stor_show_command(Scsi_Cmnd *srb)
{
	char *what = NULL;

	switch (srb->cmnd[0]) {
	case TEST_UNIT_READY: what = "TEST_UNIT_READY"; break;
	case REZERO_UNIT: what = "REZERO_UNIT"; break;
	case REQUEST_SENSE: what = "REQUEST_SENSE"; break;
	case FORMAT_UNIT: what = "FORMAT_UNIT"; break;
	case READ_BLOCK_LIMITS: what = "READ_BLOCK_LIMITS"; break;
	case REASSIGN_BLOCKS: what = "REASSIGN_BLOCKS"; break;
	case READ_6: what = "READ_6"; break;
	case WRITE_6: what = "WRITE_6"; break;
	case SEEK_6: what = "SEEK_6"; break;
	case READ_REVERSE: what = "READ_REVERSE"; break;
	case WRITE_FILEMARKS: what = "WRITE_FILEMARKS"; break;
	case SPACE: what = "SPACE"; break;
	case INQUIRY: what = "INQUIRY"; break;
	case RECOVER_BUFFERED_DATA: what = "RECOVER_BUFFERED_DATA"; break;
	case MODE_SELECT: what = "MODE_SELECT"; break;
	case RESERVE: what = "RESERVE"; break;
	case RELEASE: what = "RELEASE"; break;
	case COPY: what = "COPY"; break;
	case ERASE: what = "ERASE"; break;
	case MODE_SENSE: what = "MODE_SENSE"; break;
	case START_STOP: what = "START_STOP"; break;
	case RECEIVE_DIAGNOSTIC: what = "RECEIVE_DIAGNOSTIC"; break;
	case SEND_DIAGNOSTIC: what = "SEND_DIAGNOSTIC"; break;
	case ALLOW_MEDIUM_REMOVAL: what = "ALLOW_MEDIUM_REMOVAL"; break;
	case SET_WINDOW: what = "SET_WINDOW"; break;
	case READ_CAPACITY: what = "READ_CAPACITY"; break;
	case READ_10: what = "READ_10"; break;
	case WRITE_10: what = "WRITE_10"; break;
	case SEEK_10: what = "SEEK_10"; break;
	case WRITE_VERIFY: what = "WRITE_VERIFY"; break;
	case VERIFY: what = "VERIFY"; break;
	case SEARCH_HIGH: what = "SEARCH_HIGH"; break;
	case SEARCH_EQUAL: what = "SEARCH_EQUAL"; break;
	case SEARCH_LOW: what = "SEARCH_LOW"; break;
	case SET_LIMITS: what = "SET_LIMITS"; break;
	case READ_POSITION: what = "READ_POSITION"; break;
	case SYNCHRONIZE_CACHE: what = "SYNCHRONIZE_CACHE"; break;
	case LOCK_UNLOCK_CACHE: what = "LOCK_UNLOCK_CACHE"; break;
	case READ_DEFECT_DATA: what = "READ_DEFECT_DATA"; break;
	case MEDIUM_SCAN: what = "MEDIUM_SCAN"; break;
	case COMPARE: what = "COMPARE"; break;
	case COPY_VERIFY: what = "COPY_VERIFY"; break;
	case WRITE_BUFFER: what = "WRITE_BUFFER"; break;
	case READ_BUFFER: what = "READ_BUFFER"; break;
	case UPDATE_BLOCK: what = "UPDATE_BLOCK"; break;
	case READ_LONG: what = "READ_LONG"; break;
	case WRITE_LONG: what = "WRITE_LONG"; break;
	case CHANGE_DEFINITION: what = "CHANGE_DEFINITION"; break;
	case WRITE_SAME: what = "WRITE_SAME"; break;
	case READ_TOC: what = "READ_TOC"; break;
	case LOG_SELECT: what = "LOG_SELECT"; break;
	case LOG_SENSE: what = "LOG_SENSE"; break;
	case MODE_SELECT_10: what = "MODE_SELECT_10"; break;
	case MODE_SENSE_10: what = "MODE_SENSE_10"; break;
	case MOVE_MEDIUM: what = "MOVE_MEDIUM"; break;
	case READ_12: what = "READ_12"; break;
	case WRITE_12: what = "WRITE_12"; break;
	case WRITE_VERIFY_12: what = "WRITE_VERIFY_12"; break;
	case SEARCH_HIGH_12: what = "SEARCH_HIGH_12"; break;
	case SEARCH_EQUAL_12: what = "SEARCH_EQUAL_12"; break;
	case SEARCH_LOW_12: what = "SEARCH_LOW_12"; break;
	case READ_ELEMENT_STATUS: what = "READ_ELEMENT_STATUS"; break;
	case SEND_VOLUME_TAG: what = "SEND_VOLUME_TAG"; break;
	case WRITE_LONG_2: what = "WRITE_LONG_2"; break;
	default: break;
	}
	US_DEBUGP("Command %s (%d bytes)\n", what, srb->cmd_len);
	US_DEBUGP("%02x %02x %02x %02x "
		  "%02x %02x %02x %02x "
		  "%02x %02x %02x %02x\n",
		  srb->cmnd[0], srb->cmnd[1], srb->cmnd[2], srb->cmnd[3],
		  srb->cmnd[4], srb->cmnd[5], srb->cmnd[6], srb->cmnd[7],
		  srb->cmnd[8], srb->cmnd[9], srb->cmnd[10],
		  srb->cmnd[11]);
}

void usb_stor_print_Scsi_Cmnd( Scsi_Cmnd* cmd )
{
	int i=0, bufferSize = cmd->request_bufflen;
	u8* buffer = cmd->request_buffer;
	struct scatterlist* sg = (struct scatterlist*)cmd->request_buffer;

	US_DEBUGP("Dumping information about %p.\n", cmd );
	US_DEBUGP("cmd->cmnd[0] value is %d.\n", cmd->cmnd[0] );
	US_DEBUGP("(MODE_SENSE is %d and MODE_SENSE_10 is %d)\n",
		  MODE_SENSE, MODE_SENSE_10 );

	US_DEBUGP("buffer is %p with length %d.\n", buffer, bufferSize );
	for ( i=0; i<bufferSize; i+=16 )
	{
		US_DEBUGP("%02x %02x %02x %02x %02x %02x %02x %02x\n"
			  "%02x %02x %02x %02x %02x %02x %02x %02x\n",
			  buffer[i],
			  buffer[i+1],
			  buffer[i+2],
			  buffer[i+3],
			  buffer[i+4],
			  buffer[i+5],
			  buffer[i+6],
			  buffer[i+7],
			  buffer[i+8],
			  buffer[i+9],
			  buffer[i+10],
			  buffer[i+11],
			  buffer[i+12],
			  buffer[i+13],
			  buffer[i+14],
			  buffer[i+15] );
	}

	US_DEBUGP("Buffer has %d scatterlists.\n", cmd->use_sg );
	for ( i=0; i<cmd->use_sg; i++ )
	{
		US_DEBUGP("Length of scatterlist %d is %d.\n",i,sg[i].length);
		US_DEBUGP("%02x %02x %02x %02x %02x %02x %02x %02x\n"
			  "%02x %02x %02x %02x %02x %02x %02x %02x\n",
			  sg[i].address[0],
			  sg[i].address[1],
			  sg[i].address[2],
			  sg[i].address[3],
			  sg[i].address[4],
			  sg[i].address[5],
			  sg[i].address[6],
			  sg[i].address[7],
			  sg[i].address[8],
			  sg[i].address[9],
			  sg[i].address[10],
			  sg[i].address[11],
			  sg[i].address[12],
			  sg[i].address[13],
			  sg[i].address[14],
			  sg[i].address[15]);
	}
}

