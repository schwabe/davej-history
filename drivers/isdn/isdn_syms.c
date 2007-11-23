/* $Id: isdn_syms.c,v 1.3.2.1 1999/04/22 21:09:37 werner Exp $

 * Linux ISDN subsystem, exported symbols (linklevel).
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: isdn_syms.c,v $
 * Revision 1.3.2.1  1999/04/22 21:09:37  werner
 * Added support for dss1 diversion services
 *
 * Revision 1.3  1997/02/16 01:02:47  fritz
 * Added GPL-Header, Id and Log
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>

#ifndef __GENKSYMS__      /* Don't want genksyms report unneeded structs */
#include <linux/isdn.h>
#endif
#include "isdn_common.h"
#ifdef CONFIG_ISDN_DIVERSION
  #include <linux/isdn_divertif.h>
extern isdn_divert_if *divert_if;

static char *map_drvname(int di)
{
  if ((di < 0) || (di >= ISDN_MAX_DRIVERS)) 
    return(NULL);
  return(dev->drvid[di]); /* driver name */
} /* map_drvname */

static int map_namedrv(char *id)
{  int i;

   for (i = 0; i < ISDN_MAX_DRIVERS; i++)
    { if (!strcmp(dev->drvid[i],id)) 
        return(i);
    }
   return(-1);
} /* map_namedrv */

int DIVERT_REG_NAME(isdn_divert_if *i_div)
{
  if (i_div->if_magic != DIVERT_IF_MAGIC) 
    return(DIVERT_VER_ERR);
  switch (i_div->cmd)
    {
      case DIVERT_CMD_REL:
        if (divert_if != i_div) 
          return(DIVERT_REL_ERR);
        divert_if = NULL; /* free interface */
        MOD_DEC_USE_COUNT;
        return(DIVERT_NO_ERR);

      case DIVERT_CMD_REG:
        if (divert_if) 
          return(DIVERT_REG_ERR);
        i_div->ll_cmd = isdn_command; /* set command function */
        i_div->drv_to_name = map_drvname; 
        i_div->name_to_drv = map_namedrv; 
        MOD_INC_USE_COUNT;
        divert_if = i_div; /* remember interface */
        return(DIVERT_NO_ERR);

      default:
        return(DIVERT_CMD_ERR);   
    }
} /* DIVERT_REG_NAME */

#endif CONFIG_ISDN_DIVERSION

#if (LINUX_VERSION_CODE < 0x020111)
static int has_exported;

static struct symbol_table isdn_syms = {
#include <linux/symtab_begin.h>
        X(register_isdn),
#ifdef CONFIG_ISDN_DIVERSION
        X(DIVERT_REG_NAME),
#endif CONFIG_ISDN_DIVERSION
#include <linux/symtab_end.h>
};

void
isdn_export_syms(void)
{
	if (has_exported)
		return;
        register_symtab(&isdn_syms);
        has_exported = 1;
}

#else

EXPORT_SYMBOL(register_isdn);
#ifdef CONFIG_ISDN_DIVERSION
  EXPORT(DIVERT_REG_NAME);
#endif CONFIG_ISDN_DIVERSION

#endif
