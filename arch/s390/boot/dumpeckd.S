/*
 *  Dump boot loader for 3380/3390 DASDs
 *    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Michael Holzheu  <holzheu@de.ibm.com>
 *               Holger Smolinski <Holger.Smolinski@de.ibm.com>
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

/* General defines */

#define IPL_BS 0x1000
#define BLOCKS_PER_WRITE 64                    /* makes 256K with 4K blksize */

#ifdef ZIPL

################################################################################
# Function entry point at 0x2000 (not used for dump) is called with C linkage
#   %r2-%r3: load descriptor
#   %r3    : device subchannel id
#   %r4    : load address
################################################################################

        basr   %r1,0
        b      _not_implemented-.(%r1)
        .align 8

#else

#define DUMP_TOOL_START 0x2000

################################################################################
# one psw and two ccws:
# Re-Read enough of bootsector to start
################################################################################

.psw:   .long 0x00080000,0x80000000+_start
.ccw1:  .long 0x06000000 + DUMP_TOOL_START,0x00000000 + IPL_BS
.ccw2:  .long 0x00000000,0x00000000

#endif /* ZIPL */

################################################################################
# Program execution of the second stage boot loader starts at 0x2008
################################################################################

        .globl _start
_start: basr   %r13,0
0:      l      %r15,1f-0b(%r13)        # load end of stack address
        l      %r11,IPL_SC             # load ipl device subchannel id
        lr     %r2,%r11
        l      %r14,.Lenable_device-0b(%r13)
        basr   %r14,%r14
        bas    %r14,_get_device_characteristics-0b(%r13)
        l      %r5,.Ldev_end_rec-0b(%r13)   # endblock (first block = nr 1) 
        st     %r5,.Ldev_bpt-0b(%r13)      
        l      %r5,.Ldev_blk_size-0b(%r13)  # get blocksize
        stcm   %r5,3,.Lwrccw+2-0b(%r13)     # store blocksize into wr template
        stcm   %r5,3,.Llodata+14-0b(%r13)   # store blocksize into lodata
        l      %r14,.Ltake_dump-0b(%r13)
        basr   %r14,%r14
1:      .long  0x10000-96                   # end of stack

#ifdef ZIPL

################################################################################
# Get the device characteristics:
#
# Get device characteristics from zipl parameters (at 0x218)
# The following parameter format is expected 2x8 byte:
#
# Dump Start: CCHH RBBN
# Dump End:   CCHH RBBX
#
# - CC: Start/End Cylinder Number
# - HH: Start/End Head Number
# - R : Start/End Record Number
# - BB: Blocksize
# - N : Number of Heads of DASD
# - X : Unused
#
# Cylinder, Heads are counted starting with 0.
# Records are counted starting with 1.
# We assume that the End Record Number is at track boundary.
# This allows us to determine the number of Blocks Per Track.
################################################################################

_get_device_characteristics:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,.Lc96-0b(%r13)      # create stack frame
        la     %r12,0                   # load base register
        mvc    .Ldev_start_cyl-0b+2(2,%r13),0x218(%r12)
        mvc    .Ldev_start_hd-0b+2(2,%r13),0x21a(%r12)
        mvc    .Ldev_start_rec-0b+3(1,%r13),0x21c(%r12)
        mvc    .Ldev_blk_size-0b+2(2,%r13),0x21d(%r12)
        mvc    .Ldev_nr_hds-0b+3(1,%r13),0x21f(%r12)
        mvc    .Ldev_end_cyl-0b+2(2,%r13),0x220(%r12)
        mvc    .Ldev_end_hd-0b+2(2,%r13),0x222(%r12)
        mvc    .Ldev_end_rec-0b+3(1,%r13),0x224(%r12)
        lm     %r6,%r15,120(%r15)
        br     %r14

#else

################################################################################
# Get the device characteristics:                                              
#
# The following is fix:
# - blocksize      = 4K
# - start cylinder = 0
# - start head     = 0
# - start record   = 4
# - end cylinder   = unlimited
# - end head       = nr of heads
# - end record     = blocks per track
# 
# The following is read from the device characteristics
#
# - model 3380/90 ==> blocks per track
# - nr of heads   
#
################################################################################

_get_device_characteristics:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                   # base register
0:      s      %r15,.Lc96-0b(%r13)      # create stack frame
        la     %r6,.Lrdcccw-0b(%r13)
        st     %r6,.Lorb+8-0b(%r13)     # store cp-address to orb
        l      %r2,IPL_SC
        la     %r3,.Lorb-0b(%r13) 
        la     %r4,.Lirb-0b(%r13)
        la     %r5,2
        bas    %r14,_ssch-0b(%r13)      # start I/O: Read device characteristic 

        # find out blocks per track (bpt)

        la      %r6,9
        clc     .Lrdcdata+3-0b(2,%r13),.L9345-0b(%r13)
        be      1f-0b(%r13)
        la      %r6,10
        clc     .Lrdcdata+3-0b(2,%r13),.L3380-0b(%r13)
        be      1f-0b(%r13)
        la      %r6,12
        clc     .Lrdcdata+3-0b(2,%r13),.L3390-0b(%r13)
        be      1f-0b(%r13) 

        # not supported device panik

        la      %r2,EDEV_INVAL
        l       %r14,.Lpanik-0b(%r13)
        basr    %r14,%r14

1:   
        # store dev characteristic

        st     %r6,.Ldev_end_rec-0b(%r13)

        slr    %r6,%r6
        icm    %r6,3,.Lrdcdata+14-0b(%r13)
        st     %r6,.Ldev_end_hd-0b(%r13)
        st     %r6,.Ldev_nr_hds-0b(%r13)

        la     %r6,0
        st     %r6,.Ldev_start_cyl-0b(%r13)
        la     %r6,0
        st     %r6,.Ldev_start_hd-0b(%r13)
        la     %r6,4
        st     %r6,.Ldev_start_rec-0b(%r13)
        l      %r6,.Lblk_size-0b(%r13)
        st     %r6,.Ldev_blk_size-0b(%r13)
        l      %r6,.Lend_cyl-0b(%r13)
        st     %r6,.Ldev_end_cyl-0b(%r13)
        
        lm     %r6,%r15,120(%r15)
        br     %r14
.L3390:
        .word   0x3390
.L9345:
        .word   0x9345
.L3380:
        .word   0x3380
.Lend_cyl:
        .long   0x0000ffff
.Lblk_size:
	.long   0x1000
.align 8

.Lrdcdata:
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
.Lrdcccw:
        .long 0x64000040,0x00000000+.Lrdcdata    # read device characteristics

#endif /* ZIPL */

################################################################################
# Dump memory
################################################################################

_dump_mem:    
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      s      %r15,.Lc96-0b(%r13)            # create stack frame

        # calculate start and end block

        la      %r6,0
        l       %r7,.Ldev_start_cyl-0b(%r13)
        la      %r8,0
        l       %r9,.Ldev_start_hd-0b(%r13)
        l       %r10,.Ldev_start_rec-0b(%r13)
        l       %r11,.Ldev_nr_hds-0b(%r13)
        l       %r12,.Ldev_bpt-0b(%r13)       # = blocks per track

        # calculate start block
        #######################

        # cyl * nr_hds * blkptr

        mr      %r6,%r11
        mr      %r6,%r12

        # hd * recs

        mr      %r8,%r12

        # start block = cyl * nr_hds * bptr + hd * bptr + rec

        ar      %r7,%r9
        ar      %r7,%r10

        bctr    %r7,0                         # decrement, we start counting
                                              # beginning with block 0

        st      %r7,.Ldev_start_blk-0b(%r13)
        
        

        # calculate end block
        #######################

        l       %r7,.Ldev_end_cyl-0b(%r13)
        l       %r9,.Ldev_end_hd-0b(%r13)

        # cyl * nr_hds * blkptr

        mr      %r6,%r11
        mr      %r6,%r12

        # hd * recs

        mr      %r8,%r12

        # end block = cyl * nr_hds * bptr + hd * bptr + rec

        ar      %r7,%r9
        ar      %r7,%r12

        bctr    %r7,0                        # decrement, we start counting
                                             # beginning with block 0

        st      %r7,.Ldev_end_blk-0b(%r13)

        # calculate bytes per write (blksize * blwr)

        l       %r11,.Ldev_blk_size-0b(%r13)
        mh      %r11,.Lblocks_per_write-0b(%r13)
        st      %r11,.Lbytes_per_write-0b(%r13)

# write header

.Lheaders:                                    # write dump headers
        stck    .Ldh_time-0b(%r13)            # store time
        stidp   .Ldh_cpuid-0b(%r13)           # store cpu id

        l       %r11,.Ldev_start_blk-0b(%r13) # start block

        lr      %r2,%r11
        la      %r3,.Ldh_dumpheader-0b(%r13)  # address of dump header
        
        l       %r4,.Lheader_size-0b(%r13)
        srda    %r4,32                        # shift ==> 64 bit number        
        l       %r6,.Ldev_blk_size-0b(%r13)   # get blocksize
        
        dr      %r4,%r6                       # nr of blocks for header = 
                                              # HEADER_SIZE / BLOCKSIZE = r5
        lr      %r4,%r5
        lr      %r12,%r5                      # save nr of blocks        
        bas     %r14,_writeblock-0b(%r13)     # write block to disk
        ar      %r11,%r12                     # update block counter

.Lmemory:                                     # write memory

# write memory

        la      %r10,0                        # start at address 0

.Lmloop:        
        la      %r4,BLOCKS_PER_WRITE          # write so many blocks at a time
        lr      %r2,%r11                      # restore r2
        lr      %r3,%r10                      # restore r3
        bas     %r14,_writeblock-0b(%r13)     # write block to disk
        l       %r2,.Lbytes_per_write-0b(%r13)
        ar      %r10,%r2                      # update data address
        ah      %r11,.Lblocks_per_write-0b(%r13) # skip to next block
        l       %r3,.Ldh_mem_size+4-0b(%r13)  # get memsize
        clr     %r10,%r3                      # enough ?
        bl      .Lmloop-0b(%r13)              # branch if r10 < r3

# write end marker

.lendmarker:                                  # terminate dump file
        la      %r4,1                         # write endmaker with one block
        stck    .Ld_end_time-0b(%r13)         # store end time
        lr      %r2,%r11                      # restore r2
        la      %r3,.Ld_endmarker-0b(%r13)    # address of dump end marker
        la      %r4,1                         # write 4k at a time
        bas     %r14,_writeblock-0b(%r13)     # write block to disk

        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller
.Lbytes_per_write:  .long 0x00000000
.Lheader_size:      .long HEADER_SIZE
.Lblocks_per_write: .word BLOCKS_PER_WRITE

################################################################################
# This function writes a block number given in r2 to disk
# r2:   number of first block to write ( input by caller )
#       We start counting with Block Nr 0 !!!
# r3:   address to write data from ( input by caller )
# r4:   number of blocks to write ( input by caller )
################################################################################

_writeblock:
        stm     %r6,%r15,24(%r15)
        basr    %r13,0                        # base register
0:      s       %r15,.Lc96-0b(%r13)           # create stack frame

        # check if blocks are within range:
        
        lr      %r11,%r2
        ar      %r11,%r4                      # End block
        l       %r12,.Ldev_end_blk-0b(%r13)
        clr     %r11,%r12                     # End block < dev_end_blk ?
        bl      1f-0b(%r13)                   # no
        la      %r2,EMEM                      # if yes panik
        l       %r14,.Lpanik-0b(%r13)
        basr    %r14,%r14 
        
1:      la      %r12,.Ldeccw-0b(%r13)
        st      %r12,8+.Lorb-0b(%r13)         # store cpaddr to orb
        la      %r12,.Lwrccw-0b(%r13) 
        oi      1(%r12),0x40                  # set CC in wr template

        # first setup the write channel program

        lr      %r11,%r4                      # save number of blocks

        l       %r6,.Ldev_bpt-0b(%r13)        # load bpt to r6
        l       %r7,.Ldev_nr_hds-0b(%r13)     # load heads to r7
        la      %r5,.Lwrloop-0b(%r13)
        br      %r5 /* FIXME */
        
2:      # loop for number of block (nbl) time  

        # switch to next write CCW

        l       %r5,.Ldev_blk_size-0b(%r13)
        ar      %r3,%r5                       # add blksize to destination addr
        ah      %r12,.Lccw_size-0b(%r13)      # add sizeof(ccw) to base address
        mvc     0(8,%r12),.Lwrccw-0b(%r13)    # copy template to this ccw

.Lwrloop:       

        # build up next write CCW

        st      %r3,4(%r12)                   # store target addr to this ccw
        bct     %r4,2b-0b(%r13)               # decrement no of blks still to do
        ni      1(%r12),0x3f                  # no command chaining for last ccw

        # write CCWs are setup now               

        lr      %r4,%r11                      # restore number of blocks
        stcm    %r4,3,.Llodata+2-0b(%r13)     # store number of blocks to lodata

        # compute end block 

        ar      %r4,%r2                       # r4: ebl = blk + nbl 
        bctr    %r4,0                         # decrement r4 (last blk touched)        
        
        # compute start track and start block on track

        srda    %r2,32                        # shift ==> 64 bit number 
        dr      %r2,%r6                       # trk = blk / bpt, bot = blk % bpt 
                                              # r3: trk, r2: bot
        la      %r2,1(%r2)                    # bot++ ( we start counting at 1 )
        stcm    %r2,1,.Llodata+12-0b(%r13)    # store bot to lodata

        # compute start cylinder and head

        xr      %r2,%r2                       # clear bot                  
        dr      %r2,%r7                       # cy=trk / heads, hd=trk % heads
                                              # r3: cy, r2: hd
        sll     %r3,16                        # combine to CCHH in r3
        or      %r3,%r2
        st      %r3,.Ldedata+8-0b(%r13)       # store cchh to dedata
        st      %r3,.Llodata+4-0b(%r13)       # store cchh to lodata
        st      %r3,.Llodata+8-0b(%r13)       # store cchh to lodata

        # compute end track and end block on track

        lr      %r11,%r5                      # save r5
        srda    %r4,32                        # shift ==> 64 bit number
        dr      %r4,%r6                       # tr2 = ebl / bpt
                                              # r5: tr2, r4: bot2
        # compute end cylinder and head

        xr      %r4,%r4                       # cy2=tr2/heads, hd2=hd2 % heads
        dr      %r4,%r7                       # r5: cy2, r4: hd2
        stcm    %r5,3,.Ldedata+12-0b(%r13)    # store cy2,hd2 to dedata
        stcm    %r4,3,.Ldedata+14-0b(%r13)    # store cy2,hd2 to dedata
        lr      %r5,%r11                      # restore r5

        # CCWs are setup now, arent they?

        l       %r2,IPL_SC                    # subchannel id
        la      %r3,.Lorb-0b(%r13)
        la      %r4,.Lirb-0b(%r13)
        la      %r5,10                        # initialize retries

        bas     %r14,_ssch-0b(%r13)           # start I/O

        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller
.Lccw_size:
        .word 0x8
.align 8
.Lorb:
        .long 0x0049504c,0x0080ff00     # intparm is " IPL"
        .long 0x00000000,0x00000000
.Lirb:
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
        .long 0x00000000,0x00000000
.Ldedata:
        .long 0x80c00000,0x00000000
        .long 0x00000000,0x00000000
.Llodata:
        .long 0x01800001,0x00000000
        .long 0x00000000,0x03000000
        .long 0x12345678,0x00000000

################################################################################
# read function not implemented: return EINVAL
################################################################################

_not_implemented:
        la    %r2,22  /* EINVAL */
        lcr   %r2,%r2 /* -EINVAL */
        br    %r14

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

# device characteristics
.align 8
.Ldev_start_cyl:
        .long  0x00000000
.Ldev_start_hd:
        .long  0x00000000
.Ldev_start_rec:
        .long  0x00000000
.Ldev_blk_size:
        .long  0x00000000
.Ldev_nr_hds:
        .long  0x00000000
.Ldev_end_cyl:
        .long  0x00000000
.Ldev_end_hd:
        .long  0x00000000
.Ldev_end_rec:
        .long  0x00000000


.Ldev_start_blk:
        .long  0x00000000
.Ldev_end_blk:
        .long  0x00000000
.Ldev_bpt:
        .long  0x00000000


#
# CCWs
#

.align 8

# channel program for one write

.Ldeccw:
        .long 0x63400010,0x00000000+.Ldedata     # define extent
.Lloccw:
        .long 0x47400010,0x00000000+.Llodata     # locate record
.Lwrccw:
        .long 0x8d000000,0x00000000              # update Key & data

.org IPL_BS
