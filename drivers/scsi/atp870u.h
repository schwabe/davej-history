#ifndef _ATP870U_H

/* $Id: atp870u.h,v 1.0 1997/05/07 15:09:00 root Exp root $
 *
 * Header file for the ACARD 870U/W driver for Linux
 *
 * $Log: atp870u.h,v $
 * Revision 1.0  1997/05/07  15:09:00  root
 * Initial revision
 *
 */

#include <linux/types.h>
#include <linux/kdev_t.h>

/* I/O Port */

#define MAX_CDB 12
#define MAX_SENSE 14

int atp870u_detect(Scsi_Host_Template *);
int atp870u_command(Scsi_Cmnd *);
int atp870u_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int atp870u_abort(Scsi_Cmnd *);
int atp870u_reset(Scsi_Cmnd *, unsigned int);
int atp870u_biosparam(Disk *, kdev_t, int*);
void send_s870(unsigned char);

#define qcnt            32
#define ATP870U_SCATTER 127
#define ATP870U_CMDLUN 1

#ifndef NULL
        #define NULL 0
#endif

extern struct proc_dir_entry proc_scsi_atp870u;

extern const char *atp870u_info(struct Scsi_Host *);

extern int atp870u_proc_info(char *, char **, off_t, int, int, int);

#define ATP870U {  NULL, NULL,                          \
                     &proc_scsi_atp870u,/* proc_dir_entry */ \
                     atp870u_proc_info,                 \
                     NULL,                              \
                     atp870u_detect,                    \
                     NULL,                              \
                     atp870u_info,                      \
                     atp870u_command,                   \
                     atp870u_queuecommand,              \
                     atp870u_abort,                     \
                     atp870u_reset,                     \
                     NULL,                              \
                     atp870u_biosparam,                 \
                     qcnt,                              \
                     7,                                 \
                     ATP870U_SCATTER,                   \
                     ATP870U_CMDLUN,                    \
                     0,                                 \
                     0,                                 \
                     ENABLE_CLUSTERING}

#endif
