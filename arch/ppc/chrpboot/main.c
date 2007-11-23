/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include "../coffboot/nonstdio.h"
#include "../coffboot/zlib.h"

extern void *finddevice(const char *);
extern int getprop(void *, const char *, void *, int);
void gunzip(void *, int, unsigned char *, int *);

#define get_16be(x)	(*(unsigned short *)(x))
#define get_32be(x)	(*(unsigned *)(x))

#define RAM_START	0x00000000
#define RAM_END		(64<<20)

#define BOOT_START	((unsigned long)_start)
#define BOOT_END	((unsigned long)(_end + 0xFFF) & ~0xFFF)
#define PROG_START	0x00010000

char *avail_ram;
char *end_avail;

extern char _start[], _end[];
extern char image_data[];
extern int image_len;
extern char initrd_data[];
extern int initrd_len;


chrpboot(int a1, int a2, void *prom)
{
    int ns, oh, i;
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned initrd_start, initrd_size;

    printf("chrpboot starting: loaded at 0x%x\n\r", _start);

    if (initrd_len) {
	initrd_size = initrd_len;
	initrd_start = (RAM_END - initrd_size) & ~0xFFF;
	a1 = initrd_start;
	a2 = initrd_size;
	claim(initrd_start, RAM_END - initrd_start, 0);
	printf("initial ramdisk moving 0x%x <- 0x%x (%x bytes)\n",
	       initrd_start, initrd_data, initrd_size);
	memcpy((char *)initrd_start, initrd_data, initrd_size);
    }
    im = image_data;
    len = image_len;
    /* try and claim our text/data in case of OF bugs */
    claim(BOOT_START, BOOT_END - BOOT_START, 0);
    /* claim 4MB starting at PROG_START */
    claim(PROG_START, (4<<20) - PROG_START, 0);
    dst = (void *) PROG_START;
    if (im[0] == 0x1f && im[1] == 0x8b) {
	/* claim 512kB for scratch space */
	avail_ram = (char *) claim(0, 512 << 10, 0x10);
	end_avail = avail_ram + (512 << 10);
	printf("avail_ram = %x\n", avail_ram);
	printf("gunzipping (0x%x <- 0x%x:0x%0x)...", dst, im, im+len);
	gunzip(dst, 0x400000, im, &len);
	printf("done %u bytes\n\r", len);
    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);
    
    sa = (unsigned long)PROG_START;
    printf("start address = 0x%x\n\r", sa);

    (*(void (*)())sa)(0, 0, prom, a1, a2);

    printf("returned?\n\r");

    pause();
}

void *zalloc(void *x, unsigned items, unsigned size)
{
    void *p = avail_ram;

    size *= items;
    size = (size + 7) & -8;
    avail_ram += size;
    if (avail_ram > end_avail) {
	printf("oops... out of memory\n\r");
	pause();
    }
    return p;
}

void zfree(void *x, void *addr, unsigned nb)
{
}

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

#define DEFLATED	8

void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp)
{
    z_stream s;
    int r, i, flags;

    /* skip header */
    i = 10;
    flags = src[3];
    if (src[2] != DEFLATED || (flags & RESERVED) != 0) {
	printf("bad gzipped data\n\r");
	exit();
    }
    if ((flags & EXTRA_FIELD) != 0)
	i = 12 + src[10] + (src[11] << 8);
    if ((flags & ORIG_NAME) != 0)
	while (src[i++] != 0)
	    ;
    if ((flags & COMMENT) != 0)
	while (src[i++] != 0)
	    ;
    if ((flags & HEAD_CRC) != 0)
	i += 2;
    if (i >= *lenp) {
	printf("gunzip: ran out of data in header\n\r");
	exit();
    }

    s.zalloc = zalloc;
    s.zfree = zfree;
    r = inflateInit2(&s, -MAX_WBITS);
    if (r != Z_OK) {
	printf("inflateInit2 returned %d\n\r", r);
	exit();
    }
    s.next_in = src + i;
    s.avail_in = *lenp - i;
    s.next_out = dst;
    s.avail_out = dstlen;
    r = inflate(&s, Z_FINISH);
    if (r != Z_OK && r != Z_STREAM_END) {
	printf("inflate returned %d msg: %s\n\r", r, s.msg);
	exit();
    }
    *lenp = s.next_out - (unsigned char *) dst;
    inflateEnd(&s);
}
