/*  Generic MTRR (Memory Type Range Register) ioctls.

    Copyright (C) 1997  Richard Gooch

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.

    modified by Mathias Fr"ohlich, Jan, 1998
    <frohlich@na.uni-tuebingen.de>
*/
#ifndef _LINUX_MTRR_H
#define _LINUX_MTRR_H

/*  These are the region types  */
#define MTRR_TYPE_UNCACHABLE 0
#define MTRR_TYPE_WRCOMB     1
/*#define MTRR_TYPE_         2*/
/*#define MTRR_TYPE_         3*/
#define MTRR_TYPE_WRTHROUGH  4
#define MTRR_TYPE_WRPROT     5
#define MTRR_TYPE_WRBACK     6
#define MTRR_NUM_TYPES       7

static char *attrib_to_str (int x) __attribute__ ((unused));

static char *attrib_to_str (int x)
{
	switch (x) {
	case 0: return "uncachable";
	case 1: return "write-combining";
	case 4: return "write-through";
	case 5: return "write-protect";
	case 6: return "write-back";
	default: return "?";
	}
}   /*  End Function attrib_to_str  */

#ifdef __KERNEL__

#include <linux/config.h>

#ifdef CONFIG_MTRR

extern void check_mtrr_config(void);
extern void init_mtrr_config(void);
/* extern void set_mtrr_config(void); */

#endif /* CONFIG_MTRR */

#endif /* __KERNEL__ */

#endif /* _LINUX_MTRR_H */
