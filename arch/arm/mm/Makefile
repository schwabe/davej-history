#
# Makefile for the linux arm-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET	:= mm.o
O_OBJS		:= init.o extable.o fault-$(PROCESSOR).o small_page.o

ifeq ($(PROCESSOR),armo)
 O_OBJS		+= proc-arm2,3.o
endif

ifeq ($(PROCESSOR),armv)
 O_OBJS += mm-$(MACHINE).o proc-arm6,7.o proc-sa110.o ioremap.o
endif

include $(TOPDIR)/Rules.make

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) $(AFLAGS_$@) -traditional -c -o $*.o $<

# Special dependencies
fault-armv.o: fault-common.c
fault-armo.o: fault-common.c
proc-arm2,3.o:	../lib/constants.h
proc-arm6,7.o:	../lib/constants.h
proc-sa110.o:	../lib/constants.h

