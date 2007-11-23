/*********************************************************************
 *                
 * Filename:      irproc.c
 * Version:       1.0
 * Description:   Various entries in the /proc file system
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 21:33:24 1998
 * Modified at:   Wed Jan  5 22:45:14 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-2000, Dag Brattli <dagb@cs.uit.no>
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>, 
 *     All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     I, Thomas Davis, provide no warranty for any of this software. 
 *     This material is provided "AS-IS" and at no charge. 
 *     
 ********************************************************************/

#define __NO_VERSION__    /* irmod.c defines module_kernel_version */
#include <linux/module.h>

#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>

extern int irlap_proc_read(char *buf, char **start, off_t offset, int len, 
			   int unused);
extern int irlmp_proc_read(char *buf, char **start, off_t offset, int len, 
			   int unused);
extern int irttp_proc_read(char *buf, char **start, off_t offset, int len, 
			   int unused);
extern int irias_proc_read(char *buf, char **start, off_t offset, int len,
			   int unused);
extern int discovery_proc_read(char *buf, char **start, off_t offset, int len, 
			       int unused);

struct irda_entry {
	char *name;
	int (*fn)(char*, char**, off_t, int, int);
};

struct proc_dir_entry *proc_irda;
 
static struct irda_entry dir[] = {
	{"discovery",	discovery_proc_read},
	{"irttp",	irttp_proc_read},
	{"irlmp",	irlmp_proc_read},
	{"irlap",	irlap_proc_read},
	{"irias",	irias_proc_read},
};

#define IRDA_ENTRIES_NUM (sizeof(dir)/sizeof(dir[0]))
 
/*
 * Function irda_proc_register (void)
 *
 *    Register irda entry in /proc file system
 *
 */
void irda_proc_register(void) 
{
	int i;

	proc_irda = create_proc_entry("net/irda", S_IFDIR, NULL);
#ifdef MODULE
	proc_irda->fill_inode = &irda_proc_modcount;
#endif /* MODULE */

	for (i=0;i<IRDA_ENTRIES_NUM;i++)
		create_proc_entry(dir[i].name,0,proc_irda)->get_info=dir[i].fn;
}

/*
 * Function irda_proc_unregister (void)
 *
 *    Unregister irda entry in /proc file system
 *
 */
void irda_proc_unregister(void) 
{
	int i;

	for (i=0;i<IRDA_ENTRIES_NUM;i++)
		remove_proc_entry(dir[i].name, proc_irda);

	remove_proc_entry("net/irda", NULL);
}


