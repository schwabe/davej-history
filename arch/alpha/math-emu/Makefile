#
# Makefile for math-emulator files...
#

O_TARGET := math-emu.o
O_OBJS   := fp-emul.o faddd.o fadds.o fdivd.o fdivs.o fdtoi.o \
		fdtos.o fdtox.o fmuld.o fmuls.o fsmuld.o fsqrtd.o \
		fsqrts.o fstod.o fstoi.o fstox.o fsubd.o fsubs.o \
		cmptxx.o fxtos.o fxtod.o udivmodti4.o div128.o

LINKS	:=	double.h faddd.c fadds.c fdivd.c fdivs.c fdtoi.c \
		fdtos.c fdtox.c fmuld.c fmuls.c fsmuld.c fsqrtd.c \
		fsqrts.c fstod.c fstoi.c fstox.c fsubd.c fsubs.c \
		op-common.h op-1.h op-2.h op-4.h single.h soft-fp.h \
		udivmodti4.c



ifeq ($(CONFIG_MATHEMU),m)
M_OBJS   := $(O_TARGET)
endif

include $(TOPDIR)/Rules.make

symlinks:
	ln -sf $(patsubst %,../../sparc64/math-emu/%,$(LINKS)) .

cleansymlinks:
	rm -f $(LINKS)
