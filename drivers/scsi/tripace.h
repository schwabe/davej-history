/* tc2550.h -- Header for tripace TC-2550x PCI-SCSI HA
 * Created:  Tue June 9 by chennai team of Tripace Ravi 
 * Author: Ravi ravi01@md2.vsnl.net.in 
 * Copyright 1998 Tripace B.V 
 *

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.

 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.

 */
#define MAXSGENT  16

#ifndef _TRIPACE_H
#define _TRIPACE_H

int        tc2550_detect( Scsi_Host_Template * );
int        tc2550_command( Scsi_Cmnd * );
int        tc2550_abort( Scsi_Cmnd * );
const char *tc2550_info( struct Scsi_Host * );
int        tc2550_reset( Scsi_Cmnd * ); 
int        tc2550_queue( Scsi_Cmnd *, void (*done)(Scsi_Cmnd *) );
int        tc2550_biosparam(Disk *,int,int *) ;

#define TC2550       { NULL,                             \
		       NULL,                             \
		       NULL,		                 \
		       "tripace",                        \
		       PROC_SCSI_TRIPACE,          \
		       NULL,				 \
		       tc2550_detect,              \
		       NULL,				 \
		       tc2550_info,                \
		       tc2550_command,             \
		       tc2550_queue,               \
		       tc2550_abort,               \
		       tc2550_reset,               \
		       NULL,                             \
		       tc2550_biosparam,           \
		       1, 				 \
		       7, 				 \
		       MAXSGENT, 				 \
		       1, 				 \
		       0, 				 \
		       0, 				 \
		       DISABLE_CLUSTERING }
#endif
