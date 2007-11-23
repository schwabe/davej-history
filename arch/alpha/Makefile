#
# alpha/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

NM := nm -B

ifdef CONFIG_CROSSCOMPILE
# enable this for linking under OSF/1:
LINKFLAGS = -non_shared -T 0xfffffc0000310000 -N
else
 elf=$(shell if $(LD) --help | grep elf64alpha >/dev/null; then echo yes; fi)
 ifeq ($(elf),yes)
   LINKFLAGS = -static -Ttext 0xfffffc0000310000 -N
 else
   LINKFLAGS = -static -T arch/alpha/vmlinux.lds -N
 endif
# GNU gcc/cc1/as can use pipes instead of temporary files
CFLAGS := $(CFLAGS) -pipe
endif

CFLAGS := $(CFLAGS) -mno-fp-regs

# determine if we can use the BWX instructions with GAS
dummy:=$(shell rm -f /tmp/GAS_VER)
#$(shell $(AS) --version >& /tmp/GAS_VER)
dummy:=$(shell $(AS) --version > /tmp/GAS_VER 2>&1)
OLD_GAS := $(shell if cat /tmp/GAS_VER | grep 'version 2.7' > /dev/null; then echo yes; else echo no; fi)

ifneq ($(OLD_GAS),yes)
 CFLAGS := $(CFLAGS) -Wa,-m21164a -DBWX_USABLE

# if PYXIS, then enable use of BWIO space
 ifeq ($(CONFIG_ALPHA_PYXIS),y)
  CFLAGS := $(CFLAGS) -DBWIO_ENABLED
 endif
endif

HEAD := arch/alpha/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/alpha/kernel arch/alpha/mm arch/alpha/lib \
	arch/alpha/math-emu
ARCHIVES := arch/alpha/kernel/kernel.o arch/alpha/mm/mm.o $(ARCHIVES)
LIBS := $(TOPDIR)/arch/alpha/math-emu/math-emu.a \
	$(TOPDIR)/arch/alpha/lib/lib.a $(LIBS) $(TOPDIR)/arch/alpha/lib/lib.a

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

rawboot:
	@$(MAKEBOOT) rawboot

#
# my boot writes directly to a specific disk partition, I doubt most
# people will want to do that without changes..
#
msb my-special-boot:
	@$(MAKEBOOT) msb

bootimage:
	@$(MAKEBOOT) bootimage

srmboot:
	@$(MAKEBOOT) srmboot

archclean:
	@$(MAKEBOOT) clean

archdep:
	@$(MAKEBOOT) dep

bootpfile:
	@$(MAKEBOOT) bootpfile
