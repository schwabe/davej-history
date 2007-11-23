#
# Makefile for math-emulator files...
#

O_TARGET :=	math-emu.o
O_OBJS   :=	fp-emul.o udivmodti4.o

ifeq ($(CONFIG_MATHEMU),m)
M_OBJS   := $(O_TARGET)
endif

include $(TOPDIR)/Rules.make
