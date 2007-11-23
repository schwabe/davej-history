/*
 *	Access to VGA videoram
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 */

#ifndef _LINUX_ASM_VGA_H_
#define _LINUX_ASM_VGA_H_

#include <asm/io.h>

#define VT_BUF_HAVE_RW
#define VT_BUF_HAVE_MEMSETW
#define VT_BUF_HAVE_MEMCPYW
#define VT_BUF_HAVE_MEMCPYF

extern inline void scr_writew(u16 val, u16 *addr)
{
	if ((long) addr < 0)
		*addr = val;
	else
		writew(val, (unsigned long) addr);
}

extern inline u16 scr_readw(const u16 *addr)
{
	if ((long) addr < 0)
		return *addr;
	else
		return readw((unsigned long) addr);
}

extern inline void scr_memsetw(u16 *s, u16 c, unsigned int count)
{
	if ((long)s < 0)
		memsetw(s, c, count);
	else
		memsetw_io(s, c, count);
}

/* Try to do this the optimal way for the given destination and source. */
extern inline void scr_memcpyw(u16 *d, const u16 *s, unsigned int count)
{
    if ((long)s < 0) {	/* source is memory */
	if ((long)d < 0) memcpy(d, s, count);		/* dest is memory */
        else memcpy_toio(d, s, count);			/* dest is screen */
    } else {		/* source is screen */
	if ((long)d < 0) memcpy_fromio(d, s, count);	/* dest is memory */
	else {						/* dest is screen */
	    /* Right now, we do this the slow way,
	       as we cannot handle screen unaligned ops...
	    */
	    count /= 2;
	    while (count--)
                scr_writew(scr_readw(s++), d++);
	}
    }
}

/* Do not trust that the usage will be correct; analyze the arguments. */
#define scr_memcpyw_from scr_memcpyw
#define scr_memcpyw_to   scr_memcpyw

#define vga_readb readb
#define vga_writeb writeb

#define VGA_MAP_MEM(x) (x)

#endif
