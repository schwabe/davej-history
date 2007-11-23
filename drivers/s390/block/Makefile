all: s390-block.o

CFLAGS += 
O_TARGET := s390-block.o
O_OBJS   := 
M_OBJS   :=
MI_OBJS  :=
MIX_OBJS :=

ifeq ($(CONFIG_DASD),y)
  OX_OBJS += dasd.o
  ifeq ($(CONFIG_DASD_ECKD),y)
   O_OBJS += dasd_eckd.o  dasd_3990_erp.o dasd_9343_erp.o
  endif 
  ifeq ($(CONFIG_DASD_FBA),y)
   O_OBJS += dasd_fba.o dasd_3370_erp.o dasd_9336_erp.o
  endif 
  ifeq ($(CONFIG_DASD_MDSK),y)
   O_OBJS += dasd_diag.o
  endif 
endif

ifeq ($(CONFIG_DASD),m)
  M_OBJS += dasd_mod.o
  MIX_OBJS += dasd.o 
  ifeq ($(CONFIG_DASD_ECKD),y)
   MI_OBJS += dasd_eckd.o  dasd_3990_erp.o dasd_9343_erp.o
  endif 
  ifeq ($(CONFIG_DASD_FBA),y)
   MI_OBJS += dasd_fba.o dasd_3370_erp.o dasd_9336_erp.o
  endif 
  ifeq ($(CONFIG_DASD_MDSK),y)
   MI_OBJS += dasd_diag.o
  endif 
endif

ifeq ($(CONFIG_MDISK),y)
  O_OBJS += mdisk.o
endif

ifeq ($(CONFIG_BLK_DEV_XPRAM),y)
 O_OBJS += xpram.o
else
 ifeq ($(CONFIG_BLK_DEV_XPRAM),m)
   M_OBJS += xpram.o
 endif
endif

dasd_mod.o: $(MI_OBJS) $(MIX_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(MI_OBJS) $(MIX_OBJS)

include $(TOPDIR)/Rules.make
