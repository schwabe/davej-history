#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

# Once a gas that groks -mvec is generally available, we'll use it...
.S.o:
#ifdef CONFIG_ALTIVEC
#	$(CC) $(CFLAGS) -D__ASSEMBLY__ -Wa,-mvec -c $< -o $*.o
#else
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c $< -o $*.o
#endif

O_TARGET := kernel.o
OX_OBJS := ppc_ksyms.o setup.o


O_OBJS := traps.o irq.o idle.o time.o process.o signal.o syscalls.o misc.o \
	  bitops.o ptrace.o align.o ppc_htab.o
ifdef CONFIG_PCI
O_OBJS += pci.o
endif
ifdef CONFIG_KGDB
O_OBJS += ppc-stub.o
endif
ifdef CONFIG_TOTALMP
O_OBJS += totalmp.o
endif
ifdef CONFIG_PMAC_PBOOK
O_OBJS += sleep.o
endif

ifeq ($(CONFIG_MBX),y)
O_OBJS += mbx_setup.o mbx_pci.o softemu8xx.o i8259.o ppc8xx_pic.o
endif
ifeq ($(CONFIG_APUS),y)
O_OBJS += apus_setup.o prom.o openpic.o
endif

PMAC_OBJS = pmac_time.o pmac_setup.o pmac_support.o pmac_pci.o pmac_pic.o \
	feature.o openpic.o open_pic.o prom.o
CHRP_OBJS = $(PMAC_OBJS) chrp_time.o chrp_pci.o i8259.o indirect_pci.o
CHRPX_OBJS = chrp_setup.o
PREP_OBJS = prep_time.o prep_pci.o residual.o prep_nvram.o i8259.o \
	indirect_pci.o openpic.o open_pic.o prom.o
PREPX_OBJS = prep_setup.o

ifeq ($(CONFIG_ALL_PPC),y)
O_OBJS += $(sort $(PMAC_OBJS) $(PREP_OBJS) $(CHRP_OBJS))
OX_OBJS += $(PMACX_OBJS) $(PREPX_OBJS) $(CHRPX_OBJS)
endif
ifeq ($(CONFIG_PMAC),y)
O_OBJS += $(PMAC_OBJS)
OX_OBJS += $(PMACX_OBJS)
endif
ifeq ($(CONFIG_PREP),y)
O_OBJS += $(PREP_OBJS)
OX_OBJS += $(PREPX_OBJS)
endif
ifeq ($(CONFIG_CHRP),y)
O_OBJS += $(CHRP_OBJS)
OX_OBJS += $(CHRPX_OBJS)
endif

GEMINI_OBJS = $(PREP_OBJS) gemini_pci.o gemini_prom.o gemini_setup.o
ifeq ($(CONFIG_GEMINI),y)
O_OBJS += $(GEMINI_OBJS)
OX_OBJS += $(PREPX_OBJS)
endif

ifdef CONFIG_SMP
O_OBJS += smp.o
endif

all: head.o kernel.o

head.o: head.S $(TOPDIR)/include/linux/tasks.h ppc_defs.h

ppc_defs.h: mk_defs.c ppc_defs.head \
		$(TOPDIR)/include/asm/mmu.h \
		$(TOPDIR)/include/asm/processor.h \
		$(TOPDIR)/include/asm/pgtable.h \
		$(TOPDIR)/include/asm/ptrace.h
	$(CC) ${CFLAGS} -S mk_defs.c
	cp ppc_defs.head ppc_defs.h
	chmod 755 ppc_defs.h
	grep '^#define' mk_defs.s >> ppc_defs.h
	rm mk_defs.s

find_name : find_name.c
	$(HOSTCC) -o find_name find_name.c

checks: checks.c
	$(HOSTCC) ${CFLAGS} -D__KERNEL__ -I../../../include -o checks checks.c
	./checks

include $(TOPDIR)/Rules.make
