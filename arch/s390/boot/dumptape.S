/*
 *  Dump boot loader for 3480/3490 tape devices
 *    Copyright (C) 1999-2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Michael Holzheu (holzheu@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * Uses extern functions:
 *  - _panik
 *  - _enable_device
 *  - _take_dump
 *
 * Functions:
 *  - _dump_mem
 */

#include "dumpcommon.S"

#define IPL_BS           0x1000
#define BLOCK_SIZE       0x8000 /* 32 KB */
#define DUMP_TOOL_START  0x2000 /* tool is loaded to this address in order */
                                /* not to overwrite page 0 */

################################################################################
# The first 24 bytes are loaded by ipl to addresses 0-23. (a PSW and two CCWs)
################################################################################

        # psw  
        .long  0x00080000,0x80000000+_start
        # rewind ccw
        .long  0x07000000,0x60000001
        # ccw to load dump utility to 0x1000
        .long  0x02000000+DUMP_TOOL_START ,0x20000000+IPL_BS

.globl _start
_start: basr   %r13,0
0:      l      %r15,1f-0b(%r13)               # load end of stack address
        l      %r11,IPL_SC                    # load ipl device subchannel id
        lr     %r2,%r11
        l      %r14,.Lenable_device-0b(%r13)
        basr   %r14,%r14
        l      %r14,.Ltake_dump-0b(%r13)
        basr   %r14,%r14
1:      .long  0x10000-96                     # end of stack

################################################################################
# Dump memory
################################################################################

_dump_mem:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      s      %r15,.Lc96-0b(%r13)            # create stack frame
#
# first write a tape mark
#
        bas   %r14,_tapemark-0b(%r13)
#
# write header
#
        stck  .Ldh_time-0b(%r13)              # store time
        stidp .Ldh_cpuid-0b(%r13)             # store cpu id
        la    %r2,.Ldh_dumpheader-0b(%r13)    # start of header
        l     %r3,.Lheader_size-0b(%r13)      # size of header 
        lr    %r4,%r3                         # blocksize 
        bas   %r14,_writer-0b(%r13)
        
#
# write real storage to tape
#       

        la    %r2,0                           # start
        l     %r3,.Ldh_mem_size+4-0b(%r13)    # length
        l     %r4,.Lblock_size-0b(%r13)       # blocksize
        bas   %r14,_writer-0b(%r13)           # write page
#
# write end marker
#
        stck  .Ld_end_time-0b(%r13)           # store end time
        la    %r2,.Ld_endmarker-0b(%r13)      # address of end marker
        la    %r3,END_MARKER_SIZE             # size of end marker
        la    %r4,END_MARKER_SIZE             # blocksize
        bas   %r14,_writer-0b(%r13)
#
# write another tape mark
#
        bas   %r14,_tapemark-0b(%r13)

        lm    %r6,%r15,120(%r15)
        br    %r14                            # return to caller
.Lheader_size:      .long HEADER_SIZE
.Lblock_size:       .long BLOCK_SIZE

################################################################################
# subroutine for writing to tape
# Parameters:   
#  -r2: start address
#  -r3: length
#  -r4: blocksize
################################################################################

_writer:        
        stm   %r6,%r15,24(%r15)
        basr  %r13,0                           # base register
0:      s     %r15,.Lc96-0b(%r13)              # create stack frame

        lr    %r10,%r2                         # save start address
        lr    %r11,%r3                         # save length
        ar    %r11,%r2                         # end address
        lr    %r12,%r4                         # save blocksize 

        st    %r10,.Lccwwrite+4-0b(%r13)       # initialize CCW data addresses
        sth   %r12,.Lccwwrite+2-0b(%r13)       # setup blocksize
.Lldlp:
        l     %r2,IPL_SC                       # subchannel id
        la    %r3,.Lorbwrite-0b(%r13)          # address of orb
        la    %r4,.Lirb-0b(%r13)               # address of irb
        la    %r5,10                           # 10 retries
        bas   %r14,_ssch-0b(%r13)              # write chunk of PAGE_SIZE bytes
        
        l     %r0,.Lccwwrite+4-0b(%r13)        # update CCW data addresses
        ar    %r0,%r12                         # add block size
        st    %r0,.Lccwwrite+4-0b(%r13)
        clr   %r0,%r11                         # enough ?
        bl    .Lldlp-0b(%r13)

        lm    %r6,%r15,120(%r15)
        br    %r14                             

################################################################################
# write tapemark
################################################################################

_tapemark:
        stm   %r6,%r15,24(%r15)
        basr  %r13,0                           # base register
0:      s     %r15,.Lc96-0b(%r13)              # create stack frame

        l     %r2,IPL_SC                       # subchannel id
        la    %r3,.Lorbmark-0b(%r13)           # r12 = address of orb
        la    %r4,.Lirb-0b(%r13)               # r5 = address of irb
        la    %r5,10                           # retries
        bas   %r14,_ssch-0b(%r13)              # write a tape mark

        lm    %r6,%r15,120(%r15)
        br    %r14                             # return to caller

################################################################################
# expand Macros
################################################################################

dump_common_fn
device_fn
io_subroutines

################################################################################
# DATA
################################################################################

# extern functions

.Lpanik:
        .long      _panik
.Lenable_device:
        .long      _enable_device
.Ltake_dump:
        .long      _take_dump

# irbs, orbs etc.

        .align 8
.Lorbwrite:
        .long  0x00000000,0x0080ff00,.Lccwwrite
        .align 8
.Lorbmark:
        .long  0x00000000,0x0080ff00,.Lccwmark
        .align 8
.Lccwwrite:
        .long  0x01200000,0x00000000    
.Lccwmark:
        .long  0x1f200001,0x00000000

.Lirb:  .long  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.org IPL_BS
