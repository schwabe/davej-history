all: s390-block.o

CFLAGS += 
O_TARGET := s390-block.o
O_OBJS   :=         
M_OBJS   :=
D_OBJS   :=

ifeq ($(CONFIG_DASD),y)
  O_OBJS += dasd.o dasd_ccwstuff.o dasd_erp.o dasd_setup.o
  ifeq ($(CONFIG_PROC_FS),y)
   O_OBJS += dasd_proc.o dasd_profile.o
  endif 
  ifeq ($(CONFIG_DASD_ECKD),y)
   O_OBJS += dasd_eckd.o dasd_eckd_erp.o dasd_3990_erp.o dasd_9343_erp.o
  endif 
  ifeq ($(CONFIG_DASD_FBA),y)
   O_OBJS += dasd_fba.o
  endif 
  ifeq ($(CONFIG_DASD_MDSK),y)
   O_OBJS += dasd_mdsk.o
  endif 
#  ifeq ($(CONFIG_DASD_CKD),y)
#   O_OBJS += dasd_ckd.o
#  endif 
endif

ifeq ($(CONFIG_DASD),m)
  M_OBJS += dasd_mod.o
  D_OBJS += dasd.o dasd_ccwstuff.o dasd_erp.o dasd_setup.o
  ifeq ($(CONFIG_PROC_FS),y)
   D_OBJS += dasd_proc.o dasd_profile.o
  endif 
  ifeq ($(CONFIG_DASD_ECKD),y)
   D_OBJS += dasd_eckd.o dasd_eckd_erp.o dasd_3990_erp.o dasd_9343_erp.o
  endif 
  ifeq ($(CONFIG_DASD_FBA),y)
   D_OBJS += dasd_fba.o
  endif 
  ifeq ($(CONFIG_DASD_MDSK),y)
   D_OBJS += dasd_mdsk.o
  endif 
#  ifeq ($(CONFIG_DASD_CKD),y)
#   D_OBJS += dasd_ckd.o
#  endif 
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

dasd_mod.o: $(D_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(D_OBJS)

include $(TOPDIR)/Rules.make
