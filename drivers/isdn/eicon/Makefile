# Makefile for the eicon ISDN device driver

# The target object and module list name.

O_TARGET	:= vmlinux-obj.o

# Objects that export symbols.

export-objs	:= eicon_mod.o

# Multipart objects.

list-multi	:= eicon.o
eicon-objs	:= eicon_mod.o eicon_isa.o eicon_pci.o eicon_idi.o eicon_io.o

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DRV_EICON)    	+= eicon.o

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

eicon.o: $(eicon-objs)
	$(LD) -r -o $@ $(eicon-objs)
