all: s390-net.o

CFLAFS += 
O_TARGET := s390-net.o
O_OBJS   :=
OX_OBJS  := iucv.o
M_OBJS   :=

ifeq ($(CONFIG_CTC),y)
  O_OBJS += ctc.o
else
  ifeq ($(CONFIG_CTC),m)
  M_OBJS += ctc.o
  endif
endif

ifeq ($(CONFIG_IUCV),y)
  O_OBJS += netiucv.o
else
  ifeq ($(CONFIG_IUCV),m)
    M_OBJS += netiucv.o
  endif
endif

ifeq ($(CONFIG_LCS),y)
O_OBJS += lcs.o
else
  ifeq ($(CONFIG_LCS),m)
  M_OBJS += lcs.o
  endif
endif

include $(TOPDIR)/Rules.make
