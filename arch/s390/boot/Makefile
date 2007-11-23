#
# Makefile for the linux i386-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

OBJCOPY = $(CROSS_COMPILE)objcopy

O_TARGET := 
O_OBJS	 := 

include $(TOPDIR)/Rules.make

%.lnk: %.o
	$(LD) -Ttext 0x0 -o $@ $<

%.boot: %.lnk
	$(OBJCOPY) -O binary $< $@

silo: silo.c
	$(CROSS_COMPILE)gcc $< -o $@

dasdfmt: dasdfmt.c
	$(CROSS_COMPILE)gcc $< -o $@

image: $(CONFIGURE) $(TOPDIR)/vmlinux \
	iplfba.boot ipleckd.boot 
	$(OBJCOPY) -O binary $(TOPDIR)/vmlinux image

listing: ../../../vmlinux
	$(OBJDUMP) --disassemble --disassemble-all --disassemble-zeroes --reloc $(TOPDIR)/vmlinux > listing

dep:

clean:
	rm -f image listing silo dasdfmt iplfba.boot ipleckd.boot

