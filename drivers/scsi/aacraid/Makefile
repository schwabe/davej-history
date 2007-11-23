#
# Makefile for the Adaptec aacraid adapter driver
#

O_OBJS:=	linit.o \
		aacid.o \
		osfuncs.o \
		osddi.o \
		aachba.o \
		commctrl.o \
		comminit.o \
		commsup.o \
		dpcsup.o \
		ossup.o \
		port.o \
		rx.o \
		sap1sup.o

ifeq ($(CONFIG_SCSI_AACRAID),y)
    O_TARGET := aacraid.o
else
  ifeq ($(CONFIG_SCSI_AACRAID),m)
    MOD_LIST_NAME := AACRAID_MODULES
    M_OBJS := aacraid.o
    O_TARGET := aacraid.o
  endif
endif

EXTRA_CFLAGS += -Iinclude -I..

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s



