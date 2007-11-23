/*++
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *   comstruc.h
 *
 * Abstract: This module defines the data structures that make up the communication
 *           region for the FSA filesystem. This region is how the host based code
 *           communicates both control and data to the adapter based code.
 *
 *
 *
 --*/
#ifndef _COMM_STRUCT
#define _COMM_STRUCT

//
// Define all the constants needed for the communication interface
//

// Define how many queue entries each queue will have and the total number of
// entries for the entire communication interface. Also define how many queues
// we support.

#define NUMBER_OF_COMM_QUEUES  8   // 4 command; 4 response

#define HOST_HIGH_CMD_ENTRIES  4
#define HOST_NORM_CMD_ENTRIES  8
#define ADAP_HIGH_CMD_ENTRIES  4
#define ADAP_NORM_CMD_ENTRIES  512
#define HOST_HIGH_RESP_ENTRIES 4
#define HOST_NORM_RESP_ENTRIES 512
#define ADAP_HIGH_RESP_ENTRIES 4
#define ADAP_NORM_RESP_ENTRIES 8

#define TOTAL_QUEUE_ENTRIES  \
    (HOST_NORM_CMD_ENTRIES + HOST_HIGH_CMD_ENTRIES + ADAP_NORM_CMD_ENTRIES + ADAP_HIGH_CMD_ENTRIES + \
	    HOST_NORM_RESP_ENTRIES + HOST_HIGH_RESP_ENTRIES + ADAP_NORM_RESP_ENTRIES + ADAP_HIGH_RESP_ENTRIES)




// Set the queues on a 16 byte alignment
#define QUEUE_ALIGNMENT		16


//
// The queue headers define the Communication Region queues. These
// are physically contiguous and accessible by both the adapter and the
// host. Even though all queue headers are in the same contiguous block they will be
// represented as individual units in the data structures.
//

typedef AAC_UINT32 QUEUE_INDEX;

typedef QUEUE_INDEX *PQUEUE_INDEX;

typedef struct _QUEUE_ENTRY {

    AAC_UINT32 Size;                     // Size in bytes of the Fib which this QE points to
    AAC_UINT32 FibAddress;            	// Receiver addressable address of the FIB (low 32 address bits)

} QUEUE_ENTRY;

typedef QUEUE_ENTRY *PQUEUE_ENTRY;



// The adapter assumes the ProducerIndex and ConsumerIndex are grouped
// adjacently and in that order.
//
typedef struct _QUEUE_HEADERS {

    PHYSICAL_ADDRESS LogicalHeaderAddress;  // Address to hand the adapter to access to this queue head
    PQUEUE_INDEX ProducerIndex;              // The producer index for this queue (host address)
    PQUEUE_INDEX ConsumerIndex;              // The consumer index for this queue (host address)

} QUEUE_HEADERS;
typedef QUEUE_HEADERS *PQUEUE_HEADERS;

//
// Define all the events which the adapter would like to notify
// the host of.
//
typedef enum _ADAPTER_EVENT {
    HostNormCmdQue = 1,         // Change in host normal priority command queue
    HostHighCmdQue,             // Change in host high priority command queue
    HostNormRespQue,            // Change in host normal priority response queue
    HostHighRespQue,            // Change in host high priority response queue
    AdapNormRespNotFull,
    AdapHighRespNotFull,
    AdapNormCmdNotFull,
    AdapHighCmdNotFull,
    SynchCommandComplete,
    AdapInternalError = 0xfe    // The adapter detected an internal error shutting down

} _E_ADAPTER_EVENT;

#ifdef AAC_32BIT_ENUMS
typedef _E_ADAPTER_EVENT	ADAPTER_EVENT;
#else
typedef AAC_UINT32			ADAPTER_EVENT;
#endif

//
// Define all the events the host wishes to notify the
// adapter of.
//
typedef enum _HOST_2_ADAP_EVENT {
    AdapNormCmdQue = 1,
    AdapHighCmdQue,
    AdapNormRespQue,
    AdapHighRespQue,
    HostShutdown,
    HostPowerFail,
    FatalCommError,
    HostNormRespNotFull,
    HostHighRespNotFull,
    HostNormCmdNotFull,
    HostHighCmdNotFull,
	FastIo,
	AdapPrintfDone
} _E_HOST_2_ADAP_EVENT;

#ifdef AAC_32BIT_ENUMS
typedef _E_HOST_2_ADAP_EVENT	HOST_2_ADAP_EVENT;
#else
typedef	AAC_UINT32		HOST_2_ADAP_EVENT;
#endif

//
// Define all the queues that the adapter and host use to communicate
//

typedef enum _QUEUE_TYPES {
        HostNormCmdQueue = 1,       // Adapter to host normal priority command traffic
        HostHighCmdQueue,           // Adapter to host high priority command traffic
        AdapNormRespQueue,          // Host to adapter normal priority response traffic
        AdapHighRespQueue,          // Host to adapter high priority response traffic
        AdapNormCmdQueue,           // Host to adapter normal priority command traffic
        AdapHighCmdQueue,           // Host to adapter high priority command traffic
        HostNormRespQueue,          // Adapter to host normal priority response traffic
        HostHighRespQueue           // Adapter to host high priority response traffic
} _E_QUEUE_TYPES;

#ifdef AAC_32BIT_ENUMS
typedef _E_QUEUE_TYPES			QUEUE_TYPES;
#else
typedef	AAC_UINT32			QUEUE_TYPES;
#endif


//
// Assign type values to the FSA communication data structures
//

typedef enum _STRUCT_TYPES {
    TFib = 1,
    TQe,
	TCtPerf
} _E_STRUCT_TYPES;

#ifdef AAC_32BIT_ENUMS
typedef	_E_STRUCT_TYPES		STRUCT_TYPES;
#else
typedef	AAC_UINT32		STRUCT_TYPES;
#endif

//
// Define the priority levels the FSA communication routines support.
//

typedef enum _COMM_PRIORITIES {
    FsaNormal = 1,
    FsaHigh
} _E_COMM_PRIORITIES;

#ifdef AAC_32BIT_ENUMS
typedef _E_COMM_PRIORITIES	COMM_PRIORITIES;
#else
typedef	AAC_UINT32		COMM_PRIORITIES;
#endif



//
// Define the LIST_ENTRY structure.  This structure is used on the NT side to link
// the FIBs together in a linked list.  Since this structure gets compiled on the adapter
// as well, we need to define this structure for the adapter's use.  If '_NT_DEF_'
// is defined, then this header is being included from the NT side, and therefore LIST_ENTRY
// is already defined.
#if !defined(_NTDEF_) && !defined(_WINNT_)
typedef struct _LIST_ENTRY {
   struct _LIST_ENTRY *Flink;
   struct _LIST_ENTRY *Blink;
} LIST_ENTRY;
typedef LIST_ENTRY *PLIST_ENTRY;
#endif


//
// Define the FIB. The FIB is the where all the requested data and
// command information are put to the application on the FSA adapter.
//

typedef struct _FIB_HEADER {
    AAC_UINT32 XferState;                    // Current transfer state for this CCB
    AAC_UINT16 Command;                     // Routing information for the destination
    AAC_UINT8 StructType;                   // Type FIB
    AAC_UINT8 Flags;						  // Flags for FIB
    AAC_UINT16 Size;                        // Size of this FIB in bytes
    AAC_UINT16 SenderSize;                  // Size of the FIB in the sender (for response sizing)
    AAC_UINT32 SenderFibAddress;          	// Host defined data in the FIB
    AAC_UINT32 ReceiverFibAddress;        	// Logical address of this FIB for the adapter
    AAC_UINT32 SenderData;                	// Place holder for the sender to store data
#ifndef __midl
	union {
		struct {
		    AAC_UINT32 _ReceiverTimeStart; 	// Timestamp for receipt of fib
		    AAC_UINT32 _ReceiverTimeDone;	// Timestamp for completion of fib
		} _s;
		LIST_ENTRY _FibLinks;			// Used to link Adapter Initiated Fibs on the host
	} _u;
#else				// The MIDL compiler does not support unions without a discriminant.
	struct {		// Since nothing during the midl compile actually looks into this
		struct {	// structure, this shoudl be ok.
			AAC_UINT32 _ReceiverTimeStart; 	// Timestamp for receipt of fib
		    AAC_UINT32 _ReceiverTimeDone;	// Timestamp for completion of fib
		} _s;
	} _u;
#endif
} FIB_HEADER;


#define FibLinks			_u._FibLinks


#define FIB_DATA_SIZE_IN_BYTES (512 - sizeof(FIB_HEADER))


typedef struct _FIB {

#ifdef BRIDGE //rma
	DLQUE link;
#endif
    FIB_HEADER Header;

    AAC_UINT8 data[FIB_DATA_SIZE_IN_BYTES];		// Command specific data

} FIB;
typedef FIB *PFIB;



//
// FIB commands
//

typedef enum _FIB_COMMANDS {
    TestCommandResponse = 			1,
    TestAdapterCommand = 			2,

	// Lowlevel and comm commands

    LastTestCommand = 				100,
    ReinitHostNormCommandQueue = 	101,
    ReinitHostHighCommandQueue = 	102,
    ReinitHostHighRespQueue = 		103,
    ReinitHostNormRespQueue = 		104,
    ReinitAdapNormCommandQueue = 	105,
    ReinitAdapHighCommandQueue = 	107,
    ReinitAdapHighRespQueue = 		108,
    ReinitAdapNormRespQueue = 		109,
    InterfaceShutdown = 			110,
    DmaCommandFib = 				120,
    StartProfile = 					121,
    TermProfile = 					122,
    SpeedTest = 					123,
    TakeABreakPt = 					124,
    RequestPerfData =				125,
    SetInterruptDefTimer=           126,
    SetInterruptDefCount=           127,
    GetInterruptDefStatus=          128,
    LastCommCommand = 				129,

	// Filesystem commands

    NuFileSystem = 					300,
    UFS = 							301,
    HostFileSystem =				302,
    LastFileSystemCommand = 		303,

	// Container Commands

    ContainerCommand = 				500,
	ContainerCommand64 =			501,

	// Cluster Commands

    ClusterCommand = 				550,

	// Scsi Port commands (scsi passthrough)

    ScsiPortCommand = 				600,

	// misc house keeping and generic adapter initiated commands

    AifRequest =					700,
    CheckRevision =					701,
    FsaHostShutdown = 				702,
    RequestAdapterInfo = 			703,
    IsAdapterPaused =				704,
    SendHostTime =					705,
    LastMiscCommand =				706

} _E_FIB_COMMANDS;



typedef AAC_UINT16 FIB_COMMAND;

//
// Commands that will target the failover level on the FSA adapter
//

typedef enum _FIB_XFER_STATE {
    HostOwned 				= (1<<0),
    AdapterOwned 			= (1<<1),
    FibInitialized 			= (1<<2),
    FibEmpty 				= (1<<3),
    AllocatedFromPool 		= (1<<4),
    SentFromHost 			= (1<<5),
    SentFromAdapter 		= (1<<6),
    ResponseExpected 		= (1<<7),
    NoResponseExpected 		= (1<<8),
    AdapterProcessed 		= (1<<9),
    HostProcessed 			= (1<<10),
    HighPriority 			= (1<<11),
    NormalPriority 			= (1<<12),
    Async					= (1<<13),
    AsyncIo					= (1<<13),	// rpbfix: remove with new regime
    PageFileIo				= (1<<14),	// rpbfix: remove with new regime
    ShutdownRequest			= (1<<15),
    LazyWrite				= (1<<16),	// rpbfix: remove with new regime
    AdapterMicroFib			= (1<<17),
    BIOSFibPath				= (1<<18),
    FastResponseCapable		= (1<<19),
	ApiFib					= (1<<20)	// Its an API Fib.

} _E_FIB_XFER_STATE;


typedef enum _FSA_ERRORS {
    FSA_NORMAL                  = 0,
    FSA_SUCCESS                 = 0,
    FSA_PENDING                 = 0x01,
    FSA_FATAL                   = 0x02,
    FSA_INVALID_QUEUE           = 0x03,
    FSA_NOENTRIES               = 0x04,
    FSA_SENDFAILED              = 0x05,
    FSA_INVALID_QUEUE_PRIORITY  = 0x06,
    FSA_FIB_ALLOCATION_FAILED   = 0x07,
    FSA_FIB_DEALLOCATION_FAILED = 0x08

} _E_FSA_ERRORS;


//
// The following defines needs to be updated any time there is an incompatible change made
// to the ADAPTER_INIT_STRUCT structure.
//
#define ADAPTER_INIT_STRUCT_REVISION		3

typedef struct _ADAPTER_INIT_STRUCT {
	AAC_UINT32		InitStructRevision;
	AAC_UINT32		MiniPortRevision;
	AAC_UINT32		FilesystemRevision;
	PAAC_VOID		CommHeaderAddress;
	PAAC_VOID		FastIoCommAreaAddress;
	PAAC_VOID		AdapterFibsPhysicalAddress;
	PAAC_VOID		AdapterFibsVirtualAddress;
	AAC_UINT32		AdapterFibsSize;
	AAC_UINT32		AdapterFibAlign;
	PAAC_VOID		PrintfBufferAddress;
	AAC_UINT32		PrintfBufferSize;
	AAC_UINT32		HostPhysMemPages;		// number of 4k pages of host physical memory
	AAC_UINT32		HostElapsedSeconds;		// number of seconds since 1970.
} ADAPTER_INIT_STRUCT;
typedef ADAPTER_INIT_STRUCT *PADAPTER_INIT_STRUCT;




#endif //_COMM_STRUCT


