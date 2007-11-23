#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

ifdef CONFIG_SMP
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o
else
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o
endif

all: kernel.o head.o init_task.o

O_TARGET := kernel.o
O_OBJS   := lowcore.o entry.o bitmap.o traps.o time.o \
            setup.o sys_s390.o ptrace.o signal.o cpcmd.o \
            s390fpu.o reipl.o s390mach.o 

OX_OBJS  := debug.o ebcdic.o irq.o process.o s390_ext.o s390_ksyms.o s390dyn.o s390io.o
MX_OBJS  :=

ifdef CONFIG_SMP
OX_OBJS += smp.o
endif

ifeq ($(CONFIG_IEEEFPU_EMULATION),y)
  O_OBJS += mathemu.o floatlib.o
endif

#
# Kernel debugging
#
ifdef CONFIG_REMOTE_DEBUG
O_OBJS += gdb-stub.o #gdb-low.o 
endif



include $(TOPDIR)/Rules.make






