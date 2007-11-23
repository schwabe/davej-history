/*
 *  dumpcommon.S
 *
 *  Common routines for dump records
 *    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Michael Holzheu  <holzheu@de.ibm.com>
 *
 * Uses extern functions:
 *  - _dump_mem (device dependent function to write dump)
 *
 * Functions:
 *  - _take_dump
 */


.include "common.S"

/* General defines */
#define PAGE_SIZE        0x1000                /* 4096 */
#define HEADER_SIZE      0x1000                /* 4096 */
#define END_MARKER_SIZE  0x10
#define DUMP_END_MARKER  0x44554d50,0x5f454e44 /* DUMP_END */
#define IPL_SC           0xb8                  /* Address of ipl subchannel */
#define S390_DUMP_MAGIC  0xa8190173,0x618f23fd /* magic number */
#define ARCH_S390        0x1                   /* arch flag for s390 */
 
/* Error codes */
#define OK               0x00000000  /* Dump completed successfully */
#define EMEM             0x00000001  /* Device too small for dump */
#define EDEV_INVAL       0x00000002  /* Device not supported */

################################################################################
# Take the dump
################################################################################

.macro dump_common_fn:

_take_dump:
        stm   %r6,%r15,24(%r15)
        basr  %r13,0
.Lbase: s     %r15,.Lc96-.Lbase(%r13)         # create stack frame
        bas   %r14,_store_status-.Lbase(%r13) # store status
        bas   %r14,_count_mem-.Lbase(%r13)    # count memory 
        l     %r14,.Ldump_mem-.Lbase(%r13)
        basr  %r14,%r14                       # dump memory
        la    %r2,OK                         
        bas   %r14,_panik-.Lbase(%r13)        # everything ok: stop now

################################################################################
# Find out memory size:
# When accessing a page which is not there, we get a program check
################################################################################

_count_mem:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      s      %r15,.Lc96-0b(%r13)            # create stack frame
        slr    %r9,%r9                        # base register for zero page
        mvc   104(8,%r9),.Lcount_mem_psw-0b(%r13) # setup program check new psw
        slr   %r10,%r10
        la    %r11,1
        sll   %r11,20                         # 1 MB
.Lloop0:
        l     %r12,0(%r10)                    # test page
        ar    %r10,%r11                       # add 1M
        bnm   .Lloop0-0b(%r13)                # r10 < 0x80000000 -> loop
.Lchkmem0:
        n     %r10,.L4malign0-0b(%r13)        # align to multiples of 4M
        st    %r10,.Ldh_mem_size+4-0b(%r13)   # store memory size
        st    %r10,.Ldh_mem_end+4-0b(%r13)    # store memory end
        srl   %r10,12                         # calculate page count (/ 4096)
        st    %r10,.Ldh_num_pages-0b(%r13)    # store page count
        mvc   88(32,%r9),.Lnew_psws-0b(%r13)  # restore disabled wait new psws
        lm    %r6,%r15,120(%r15)
        br    %r14
.Lcount_mem_psw:  .long 0x00080000,0x80000000 + .Lchkmem0
.Lnew_psws:
.long  0x000a0000,0x00000058                  # external new psw
.long  0x000a0000,0x00000060                  # svc new psw
.long  0x000a0000,0x00000068                  # program check new psw
.long  0x000a0000,0x00000070                  # machine check new psw
.L4malign0:       
.long 0xffc00000

################################################################################
# store status of all cpus in their lowcores
################################################################################


_store_status:
        stm   %r6,%r15,24(%r15)
        basr  %r13,0                          # base register
0:      s     %r15,.Lc96-0b(%r13)
        la    %r7,0x0                         # base register for 0 page

        ######## move lowcore info (assume user has made store  ########
        ######## status) to prefix-page                         ########

        lr    %r2,%r7                         # zero page (source)
        bas   %r14,_copy_lowcore-0b(%r13)

        ######## stop all cpus and store status in prefix pages ########

.Lstore_all:
        la    %r8,0                           # first cpu
        stap  .Lcurrent_cpu+2-0b(%r13)        # store current cpu address

.Lstatus_loop:
        cl    %r8,.Lcurrent_cpu-0b(%r13)      # is ipl cpu ?
        be    .Lnext_cpu-0b(%r13)             # if yes get next cpu
.Lstop_loop:
        sigp  %r9,%r8,0x5                     # stop cpu
        bc    8,.Lcpu_stopped-0b(%r13)        # accepted
        bc    4,.Lnext_cpu-0b(%r13)           # status stored in register: 
                                              # next cpu
        bc    2,.Lstop_loop-0b(%r13)          # busy:   try again
        bc    1,.Lnext_cpu-0b(%r13)           # not op: next cpu
.Lcpu_stopped:
        lr    %r9,%r7
        sigp  %r9,%r8,0xe                     # store status of cpu
        bc   8,.Lcpu_stored-0b(%r13)          # accepted
        bc   4,.Lnext_cpu-0b(%r13)            # status stored: next cpu
        bc   2,.Lcpu_stopped-0b(%r13)         # busy:          try again
        bc   1,.Lnext_cpu-0b(%r13)            # not op:        next cpu
.Lcpu_stored:
        lr    %r2,%r7                         # zero page (source)
        bas   %r14,_copy_lowcore-0b(%r13)
.Lnext_cpu:
        la    %r8,1(%r8)                      # next cpu (r8 +=1)
        cl    %r8,.Llast_cpu-0b(%r13)         # is last possible cpu ?
        bl    .Lstatus_loop-0b(%r13)          # jump if not last cpu
.Lstore_status_exit:
        lm      %r6,%r15,120(%r15)
        br      %r14                          # return to caller
.Lcurrent_cpu:    
        .long 0x0
.Llast_cpu:       
        .long 0x0000ffff

############1###################################################################
# copy lowcore to prefix page
# r2: address of source lowcore (input by caller)
################################################################################

_copy_lowcore:
        stm    %r6,%r15,24(%r15)
        basr   %r13,0                         # base register
0:      s      %r15,.Lc96-0b(%r13)

        l     %r3,0x108(%r2)                  # get prefix page from lowcore

        ###### check if lowcore address looks valid ######

        cl    %r3,.Llinux_start-0b(%r13)      # looks valid ?
        bl    .Lcpy_locore_exit-0b(%r13)      # if < linux-start addr
        l     %r6,.Lpage_align-0b(%r13)       # check page alignment
        nr    %r3,%r6
        cl    %r3,0x108(%r2)
        bnz   .Lcpy_locore_exit-0b(%r13)      # if not page aligned

        ###### copy lowcore                         ######

        # |-----------------------------------------------------------|
        # | Decimal |  Length   | Data                                | 
        # | Address |  in Bytes |                                     | 
        # |_________|___________|_____________________________________| 
        # | 212     | 4         | Extended save area address          | 
        # | 216     | 8         | CPU timer                           | 
        # | 224     | 8         | Clock comparator                    | 
        # | 256     | 8         | Current PSW                         | 
        # | 264     | 4         | Prefix register                     | 
        # | 288     | 64        | Access registers 0 through 15       | 
        # | 352     | 32        | Floating-point registers 0 through 6| 
        # | 384     | 64        | General registers 0 through 15      | 
        # | 448     | 64        | Control registers 0 through 15      | 
        # |_________|___________|_____________________________________| 

        mvc   212(20,%r3),212(%r2)      
        mvc   256(12,%r3),256(%r2)
        mvc   288(224,%r3),288(%r2)

.Lcpy_locore_exit:
        lm    %r6,%r15,120(%r15)
        br    %r14                            # return to caller
.Lpage_align:
        .long -4096 

.align 4
.Ldump_mem:       .long _dump_mem             # address of function

.Llinux_start:
        .long  0x10000
.Lc96:  .long  96 # for creating stackframes
.align 8
#
# The Dump header
#
.Ldh_dumpheader:
.Ldh_magic_number:.long S390_DUMP_MAGIC
.Ldh_version:     .long 0x00000001
.Ldh_header_size: .long HEADER_SIZE
.Ldh_dump_level:  .long 0x00000004              # DUMP_ALL
.Ldh_page_size:   .long PAGE_SIZE
.Ldh_mem_size:    .long 0x00000000,0x00000000
.Ldh_mem_start:   .long 0x00000000,0x00000000
.Ldh_mem_end:     .long 0x00000000,0x00000000
.Ldh_num_pages:   .long 0x00000000
.Ldh_pad:         .long 0x00000000
.Ldh_time:        .long 0x00000000,0x00000000
.Ldh_cpuid:       .long 0x00000000,0x00000000
.Ldh_arch:        .long ARCH_S390
.Ldh_free:        .long 0x00000000
#
# Dump End Marker
#
.align 8
.Ld_endmarker:    .long DUMP_END_MARKER
.Ld_end_time:     .long 0x00000000,0x00000000

.endm
