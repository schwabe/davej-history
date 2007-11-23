/*
 * linux/include/asm-arm/arch-ebsa110/io.h
 *
 * Copyright (C) 1997,1998 Russell King
 *
 * Modifications:
 *  06-Dec-1997	RMK	Created.
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

/*
 * This architecture does not require any delayed IO, and
 * has the constant-optimised IO
 */
#undef	ARCH_IO_DELAY

/*
 * Note that the translation here is weird -
 * the ISA mem space and some peripherals (ethernet and VG468)
 * appear as a 16-bit memory on a 32-bit bus.  This means that
 * byte lanes 2 and 3 are not used, and this translation must
 * be used.
 */
#define __isa_addr(x)		(((x) & 1) | (((x) & 0xfffffffe)) << 1)

#define __isa_io_addr(p)	(PCIO_BASE + ((p) << 2))

#define __inb(p)	(*(volatile unsigned char *)__isa_io_addr(p))
#define __inl(p)	(panic("__inl(%X) called", p),0)

extern __inline__ unsigned int __inw(unsigned int port)
{
	unsigned int value;
	__asm__ __volatile__(
	"ldr%?h	%0, [%1]	@ inw"
	: "=r" (value)
	: "r" (__isa_io_addr(port)));
	return value;
}

#define __outb(v,p)	(*(volatile unsigned char *)__isa_io_addr(p) = v)
#define __outl(v,p)	panic("__outl(%X,%X) called", v, p)

extern __inline__ unsigned int __outw(unsigned int value, unsigned int port)
{
	__asm__ __volatile__(
	"str%?h	%0, [%1]	@ outw"
	: : "r" (value), "r" (__isa_io_addr(port)));
}

#define __ioaddr(p)	__isa_io_addr(p)

/*
 * ioremap support - basic support only
 */
#define ioremap(addr,size)	((void *)(addr))
#define iounmap(addr)

#define __isa_mem_addr(x)	((void *)(0xe0000000 + __isa_addr((unsigned long)(x))))

#define readb(addr)		(*(volatile unsigned char *)__isa_mem_addr(addr))
#define readw(addr)		(*(volatile unsigned short *)__isa_mem_addr(addr))
#define readl(addr)		(*(volatile unsigned long *)__isa_mem_addr(addr))

#define writeb(b,addr)		(*(volatile unsigned char *)__isa_mem_addr(addr) = (b))
#define writew(b,addr)		(*(volatile unsigned short *)__isa_mem_addr(addr) = (b))
#define writel(b,addr)		(*(volatile unsigned long *)__isa_mem_addr(addr) = (b))

#define memset_io(a,b,c)	__ebsa110_set_isamem(__isa_mem_addr(a),(b),(c))
#define memcpy_fromio(a,b,c)	__ebsa110_copy_fromisamem((a),__isa_mem_addr(b),(c))
#define memcpy_toio(a,b,c)	__ebsa110_copy_toisamem(__isa_mem_addr(a),(b),(c))


#endif
