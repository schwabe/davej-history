# File: drivers/xpds/Makefile
#
# Makefile for Xpeed DSL NIC driver
#

ifeq ($(CONFIG_XPEED),y)
    O_TARGET := xpds-fr.o
    O_OBJS = xpds.o xpds-fsm.o xpds-sdsl.o xpds-encap-fr.o
else
  ifeq ($(CONFIG_XPEED),m)
    MOD_LIST_NAME := NET_MODULES
    M_OBJS := xpds-fr.o
    O_TARGET := xpds-fr.o
    O_OBJS = xpds.o xpds-fsm.o xpds-sdsl.o xpds-encap-fr.o
  endif
endif

EXTRA_CFLAGS += -I. -DDEBUG=0

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s
