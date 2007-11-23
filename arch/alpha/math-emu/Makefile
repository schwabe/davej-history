#
# Makefile for math-emulator files...
#

O_TARGET :=	math-emu.o
O_OBJS   :=	fp-emul.o udivmodti4.o

LINKS	:=	double.h op-common.h op-1.h op-2.h op-4.h single.h \
		soft-fp.h udivmodti4.c

ifeq ($(CONFIG_MATHEMU),m)
M_OBJS   := $(O_TARGET)
endif

include $(TOPDIR)/Rules.make

symlinks:
	ln -sf $(patsubst %,../../sparc64/math-emu/%,$(LINKS)) .

cleansymlinks:
	rm -f $(LINKS)
