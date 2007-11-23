#
# Makefile for the Macintosh-specific device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)

O_TARGET := macintosh.o
M_OBJS   :=

ifdef CONFIG_POWERMAC
O_OBJS   := via-cuda.o macio-adb.o via-pmu.o mediabay.o
OX_OBJS  := adb.o
endif

ifeq ($(CONFIG_MAC_SERIAL),y)
  O_OBJS += macserial.o
else
  ifeq ($(CONFIG_MAC_SERIAL),m)
    M_OBJS += macserial.o
  endif
endif

ifeq ($(CONFIG_NVRAM),y)
  O_OBJS += nvram.o
else
  ifeq ($(CONFIG_NVRAM),m)
    M_OBJS += nvram.o
  endif
endif

ifeq ($(CONFIG_PPC_RTC),y)
  O_OBJS += rtc.o
else
  ifeq ($(CONFIG_PPC_RTC),m)
    M_OBJS += rtc.o
  endif
endif

ifdef CONFIG_MAC_KEYBOARD
  O_OBJS += mac_keyb.o
endif
ifdef CONFIG_INPUT_ADBHID
  O_OBJS += adbhid.o
endif
ifdef CONFIG_MAC_HID
  O_OBJS += mac_hid.o
endif

include $(TOPDIR)/Rules.make

# Integrated in mac_keyb.c
# mackeymap.map is retained for historical reasons
#mackeymap.c: mackeymap.map
#	loadkeys --mktable mackeymap.map > mackeymap.c
