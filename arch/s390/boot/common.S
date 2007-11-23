#
#  Common code for the different second stage boot loaders
#    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
#    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
#

# some definitions from kernel land
__LC_IPLDEV  = 0xC6C
IPL_DEVICE   = 0x10404
INITRD_START = 0x1040C
INITRD_SIZE  = 0x10414
COMMAND_LINE = 0x10480

# error codes shown as address in disabled wait psw

EENABLE_DEV  = 0x00000100 # enable device failed
EDISABLE_DEV = 0x00000101 # disable device failed
ESSCH        = 0x00000102 # start subchannel failed

	.macro stage2_start
#
# Function entry point at 0x2000 is called with C linkage
#   %r2-%r3: load descriptor
#   %r3    : device subchannel id
#   %r4    : load address
#
        basr   %r1,0
        b      _load_blocklist-.(%r1)
	.align 8

#
# Program execution of the second stage boot loader starts at 0x1008
#
        .globl _start
_start: stm    %r0,%r15,0x180           # store all registers 
        basr   %r13,0
0:      l      %r15,3f-0b(%r13)         # load end of stack address
        l      %r11,0xb8                # load ipl device subchannel id
	lr     %r2,%r11
	bas    %r14,_enable_device-0b(%r13)
	lm     %r2,%r3,STAGE2_DESC      # get block with load descriptors
	lr     %r4,%r11
	l      %r5,4f-0b(%r13)
	la     %r12,0(%r5)              # FIXME: provide the correct offset
        bas    %r14,_load_direct-0b(%r13)
1:      lm     %r2,%r5,0(%r12)          # load 16 bytes = one load descriptor
        cl     %r4,6f-0b(%r13)          # check type range
        bh     2f
        sll    %r4,2
        l      %r1,5f-0b(%r4,%r13)      # function pointer according to type
	lr     %r4,%r11                 # pass subchannel id
        bas    %r14,0(%r1)
2:	la     %r12,16(%r12)            # next load descriptor
        b      1b-0b(%r13)
3:      .long  0x10000-96
4:	.long  0x8000
5:      .long  _load_kernel             # type 0: kernel image
	.long  _load_parmfile           # type 1: parameter file
	.long  _load_ramdisk            # type 2: initial ramdisk
	.long  _jump_target             # type 3: jump to target
6:      .long  3                        # highest index in list

_load_kernel:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,1f-0b(%r13)         # create stack frame
        lr     %r12,%r4
	bas    %r14,_load_blocklist-0b(%r13)
	st     %r12,__LC_IPLDEV         # old ipl device storage location
	l      %r6,2f-0b(%r13)          # load address of IPL_DEVICE
	st     %r12,0(%r6)              # new ipl device storage location
        l      %r6,3f-0b(%r13)          # load address of INITRD_START
        xc     0(4,%r6),0(%r6)          # clear INITRD_START
        l      %r6,4f-0b(%r13)          # load address of INITRD_SIZE
        xc     0(4,%r6),0(%r6)          # clear INITRD_SIZE
        lm     %r6,%r15,120(%r15)
        br     %r14
1:	.long  96
2:	.long  IPL_DEVICE
3:	.long  INITRD_START
4:	.long  INITRD_SIZE

_load_parmfile:                         # load parameter file to 0x10480
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,1f-0b(%r13)         # create stack frame
        bas    %r14,_load_blocklist-0b(%r13)
	l      %r1,12(%r12)
	l      %r2,2f-0b(%r13)          # load address of kernel command line
	mvc    0x0(256,%r2),0(%r1)      # move command line to 0x10480
	mvc    0x100(256,%r2),0x100(%r1)
	mvc    0x200(256,%r2),0x200(%r1)
	mvc    0x300(127,%r2),0x300(%r1)
	xc     0x37f(1,%r2),0x37f(%r2)
        lm     %r6,%r15,120(%r15)
        br     %r14
1:	.long  96
2:	.long  COMMAND_LINE

_load_ramdisk:                          # load initial ramdisk
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,1f-0b(%r13)         # create stack frame
        lr     %r12,%r5                 # save load address
        bas    %r14,_load_blocklist-0b(%r13)
	l      %r1,2f-0b(%r13)          # ramdisk start storage location
	st     %r12,0(%r1)              # store start of ramdisk
	slr    %r2,%r12
	l      %r1,3f-0b(%r13)          # ramdisk size storage location
	st     %r2,0(%r1)               # store size of ramdisk
        lm     %r6,%r15,120(%r15)
        br     %r14
1:	.long  96
2:	.long  INITRD_START
3:	.long  INITRD_SIZE

_jump_target:
        basr   %r1,0
0:      lr     %r2,%r11
        lr     %r14,%r5                 # make branch target return address
	b      _disable_device-0b(%r1)

        .endm

#
# The load descriptor is 16 bytes in length and contains 3 entries:
#   offset 0 : a blocklist descriptor (fba or eckd)
#   offset 8 : a type 
#   offset 12: an address
# The meaning of the address and the validity of the blocklst
# depends on the type.
#   type = 0 : kernel image, blocklist valid, address = load address
#   type = 1 : parameter line, blocklist valid, address = load address
#   type = 2 : initial ramdisk, blocklist valid, address = load address
#   type = 3 : jump command, blocklist invalid, address = branch address
#
	.macro blocklist_traverser
# parameter
#   %r2+%r3: blocklist head descriptor
#   %r4    : device subchannel id
#   %r5    : load address
_load_blocklist:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,4f-0b(%r13)         # create stack frame
1:	lr     %r12,%r4                 # save device subchannel id
	lr     %r11,%r5                 # save load address
	lr     %r8,%r2                  # save descriptor
	lr     %r9,%r3
	bas    %r14,_extract_length-0b(%r13)   # get length from descriptor
	lr     %r10,%r2                 # save returned length
	lr     %r2,%r8                  # reload descriptor to %r2/%r3
	lr     %r3,%r9
	lr     %r4,%r12                 # reload device id to %r4
	l      %r5,6f-0b(%r13)          # get memory area for indirect block
        bas    %r14,_load_direct-0b(%r13) # load indirect block
        lr     %r5,%r11                 # restore load address
	lr     %r9,%r10                 # (length / 8 - 1) = # direct descr.
	srl    %r9,3
	bctr   %r9,0
	l      %r8,6f-0b(%r13)
2:	clc    0(8,%r8),5f-0b(%r13)     # test block descriptor
	be     3f-0b(%r13)              # descriptor == 0 -> done
	lm     %r2,%r3,0(%r8)           # pass block descriptor
	lr     %r4,%r12                 # pass device subchannel id
        bas    %r14,_load_direct-0b(%r13) # load indirect block
	lr     %r5,%r2                  # move updated load address
	la     %r8,8(%r8)               # next descriptor
	bct    %r9,2b-0b(%r13)
	lm     %r2,%r3,0(%r8)           # load continuation descriptor
	lr     %r4,%r12                 # move device id for next round
	clc    0(8,%r8),5f-0b(%r13)     # test continuation descriptor
	bne    1b-0b(%r13)              # != 0 -> load next indirect block
3:      lr     %r2,%r5                  # return updated load address
	lm     %r6,%r15,120(%r15)
        br     %r14
4:      .long  96
5:      .long  0,0
6:      .long  0x8200                   # memory address for indirect blocks

        .endm

	.macro device_fn
#
# Enable I/O on the ipl device. 
#   %r2 : device subchannel id
#
_enable_device:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,1f-0b(%r13)
	lr     %r1,%r2
	l      %r2,4f-0b(%r13)          # set panik code early
        stsch  2f-0b(%r13)
        oi     2f-0b+5(%r13),0x84       # enable ssch and multipath mode
        msch   2f-0b(%r13)
	bnz    _panik-0b(%r13)          # subchannel busy or in error ?
	lctl   %c6,%c6,3f-0b(%r13)      # enable all interrupt classes
        lm     %r6,%r15,120(%r15)
        br     %r14
1:	.long  96
	.align 8
2:	.fill  64,1,0
3:	.long  0xff000000               # CR6 initial value
4:      .long  EENABLE_DEV

#
# Disable I/O on the ipl device.
#   %r2 : device subchannel id
#
_disable_device:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,1f-0b(%r13)
	lr     %r1,%r2
	l      %r2,3f-0b(%r13)          # set panik code early
	lctl   %c6,%c6,2f-0b(%r13)      # disable all interrupt classes
        stsch  2f-0b(%r13)
        ni     2f-0b+5(%r13),0x7B       # disable ssch and multipath mode
        msch   2f-0b(%r13)
	bnz    _panik-0b(%r13)          # subchannel busy or in error ?
        lm     %r6,%r15,120(%r15)
        br     %r14
1:	.long  96
        .align 8
2:	.long  0x00000000               # CR6 (all interrupts classes disabled)
3:      .long  EDISABLE_DEV
	.endm	

	.macro io_subroutines
#
# Start I/O
#   %r2 : device subchannel id
#   %r3 : address of orb
#   %r4 : address of irb
#   %r5 : retry count
#
_ssch:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,6f-0b(%r13)         # create stack frame
	lr     %r12,%r2                 # save subchannel id
	lr     %r11,%r3                 # save orb
	lr     %r10,%r4                 # save irb
	lr     %r9,%r5                  # save retry count
1:	lr     %r1,%r12
	ssch   0(%r11)                  # go
        bnz    4f-0b(%r13)              # houston, we have a problem
2:	lr     %r2,%r12                 # call _wait4de with subchannel id
	lr     %r3,%r10                 # and irb address as parameters
	bas    %r14,_wait4de-0b(%r13)   # wait until DE or error
	tm     9(%r10),0xff             # test channel status
	bnz    4f-0b(%r13)
	tm     8(%r10),0xf3             # test device status
	bz     5f-0b(%r13)
	bct    %r9,1b-0b(%r13)          # something went wrong, retry.
4:	l      %r2,7f-0b(%r13)
	bas    %r4,_panik-0b(%r13)      # won't return
5:	lm     %r6,%r15,120(%r15)
        br     %r14
6:	.long  96
7:      .long  ESSCH

#
# Wait for interrupt subroutine
#   %r2 : device subchannel id
#   %r3 : address of irb
#
_wait4de:
	lr     %r1,%r2
	basr   %r4,0
0:      mvc    0x78(8),5f-0b(%r4)       # set i/o new psw
1:	lpsw   4f-0b(%r4)
2:      c      %r1,0xb8                 # compare subchannel id
        bne    1b-0b(%r4)               # unequal -> continue waiting
	tsch   0(%r3)
	tm     9(%r3),0xff              # test channel status
	bnz    3f-0b(%r4)
	tm     8(%r3),0xf3              # got something unusual ?
	bnz    3f-0b(%r4)
        tm     8(%r3),0x04              # got device end ?
        bz     1b-0b(%r4)               # still busy -> continue waiting
3:      br     %r14
        .align 8
4:	.long  0x020a0000,0x80000000+1b
5:	.long  0x00080000,0x80000000+2b # io new psw

#
# Panik routine. Loads a disabled wait psw
#   %r2 : panik code
#
_panik:
	basr   %r1,0
0:	st     %r2,1f-0b+4(%r1)         # store code in address part of psw
	lpsw   1f-0b(%r1)
	.align 8
1:	.long  0x000a0000,0x00000000
        .endm
