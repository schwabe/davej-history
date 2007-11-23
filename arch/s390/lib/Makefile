#
# Makefile for s390-specific library files..
#

ifdef SMP
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o
else
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o
endif

L_TARGET = lib.a
L_OBJS = checksum.o delay.o memset.o strcmp.o strncpy.o uaccess.o

include $(TOPDIR)/Rules.make

