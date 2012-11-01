/****************************************************************************
** COPYRIGHT (C) 1994-1997 INTEL CORPORATION                               **
** DEVELOPED FOR MICROSOFT BY INTEL CORP., HILLSBORO, OREGON               **
** HTTP://WWW.INTEL.COM/                                                   **
** THIS FILE IS PART OF THE INTEL ETHEREXPRESS PRO/100B(TM) AND            **
** ETHEREXPRESS PRO/100+(TM) NDIS 5.0 MINIPORT SAMPLE DRIVER               **
****************************************************************************/

/****************************************************************************
Module Name:
    Rltk_1G.h  (821G.h)

This driver runs on the following hardware:
    - realtek 1Gig ethernet adapters

Environment:
    Kernel Mode - Or whatever is the equivalent on WinNT

Revision History
*****************************************************************************/

#ifndef _RLTK_1G_H
#define _RLTK_1G_H

#define NIC_MAX_RECV_FRAME_SIZE             1600
#define NIC_MAX_XMIT_FRAME_SIZE             1600



//- Interrupt ACK fields
#define SCB_ACK_MASK            SCB_INT_MASK   // ACK Mask


//-------------------------------------------------------------------------
// SCB Command Word bit definitions
//-------------------------------------------------------------------------
//- CUC fields
#define SCB_XMIT_START           BIT_4           // CU Start
#define SCB_XMIT_RESUME          BIT_5           // CU Resume
#define SCB_CUC_DUMP_ADDR       BIT_6           // CU Dump Counters Address
#define SCB_CUC_DUMP_STAT       (BIT_4 | BIT_6) // CU Dump statistics counters
#define SCB_TBD_LOAD_BASE       (BIT_5 | BIT_6) // Load the CU base
#define SCB_CUC_DUMP_RST_STAT   BIT_4_6         // CU Dump and reset statistics counters

//- RUC fields
#define SCB_RUC_START           BIT_0           // RU Start
#define SCB_RUC_ABORT           BIT_2           // RU Abort
#define SCB_RUC_LOAD_BASE       (BIT_1 | BIT_2) // Load the RU base

// Interrupt fields 
#define SCB_INT_MASK            r1000_intr_mask   // Mask interrupts


//-------------------------------------------------------------------------
// Receive Frame Descriptor Fields
//-------------------------------------------------------------------------

#define RECV_BUFFER_STATUS_OK           RxRES          // RECV_BUFFER OK Bit
#define RECV_BUFFER_ACT_COUNT_MASK      BIT_0_13        // RECV_BUFFER Actual Count Mask

//----------------------------------------------------------------------------
//		since all the structures below are shared memory structures between 
//						H/W and host 
//-----------------------------------------------------------------------------
#pragma pack(1)

//-------------------------------------------------------------------------
// 8169/8111/8110/8168 chip level Data Structures
//-------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Control/Status Registers (CSR)  currently NOT USED. 
//		MMIO not used. I/O access used instead by calling
//		functions RTL_xdatasize where x=R/W
		//							and datasize=8/16/32
//-----------------------------------------------------------------------------
typedef struct _CSR_STRUC {
 
	UCHAR		CSRrtk [ 1 ] ;   // dummy . not used anywhere
	
} CSR_STRUC, *PCSR_STRUC ;

//-------------------------------------------------------------------------
// Error Counters
//-------------------------------------------------------------------------
typedef struct _ERR_COUNT_STRUC {
    UINT64			XmtGoodFrames;          // Good frames transmitted
    UINT64			RcvGoodFrames;          // Good frames received
	UINT64			RxER ;
	UINT32			TxER ;
    UINT16			RcvOverrunErrors;       // Overrun errors - bus was busy
    UINT16			RcvAlignmentErrors;     // Receives that had alignment errors
    UINT32			XmtSingleCollision;     // Transmits that had 1 and only 1 coll.
    UINT32			XmtTotalCollisions;     // Transmits that had 1+ collisions.
	
	// even in 64-bit PFs no problem. NO need for __unaligned macro later
	// this will be 64-bit aligned because of 32+16+16+32+32 above
	// and the structure start guaranteed to be 64-bit aligned
	UINT64			RxOKPh ;
	UINT64			RxOKBrd ;
	UINT32			RxOKmu ;
	UINT16			TxAbt ;
    UINT16			XmtUnderruns;           // Transmit underruns (fatal or re-transmit)

	// 64-bit alignment NO problem as explained above
	UINT64			CmdBits ;

} ERR_COUNT_STRUC, *PERR_COUNT_STRUC;

//-------------------------------------------------------------------------
// Transmit Buffer Descriptor (TBD)
//-------------------------------------------------------------------------
typedef struct _TBD_STRUC {
	UINT16		FrameLength ;
	UINT16		status;
	UINT32		notused;
	UINT32      TbdBufferAddress;       // Physical Transmit Buffer Address
	UINT32		TbdBufferAddressHigh ;
} TBD_STRUC, *PTBD_STRUC;


//-------------------------------------------------------------------------
// realtek Receive BUffer (RECV_BUFFER)
//-------------------------------------------------------------------------
typedef struct _HW_RECV_BUFFER {
	
	UCHAR RecvBuffer[NIC_MAX_RECV_FRAME_SIZE];      // Data buffer in RECV_BUFFER

} HW_RECV_BUFFER, *PHW_RECV_BUFFER;


//-------------------------------------------------------------------------
// Realtek Receive Buffer  Descriptor. 
//-------------------------------------------------------------------------
typedef struct _RTK_RECEIVE_BUFFER_DESCRIPOR_STRUC {
	UINT32		status;
	UINT32		vlan_tag;
	UINT32		buf_addr;
	UINT32		buf_Haddr;
} HW_RBD, *PHW_RBD;

#pragma pack()

//-------------------------------------------------------------------------
// 821G PCI Register Definitions
// Refer To The PCI Specification For Detailed Explanations
//-------------------------------------------------------------------------
//- Register Offsets
#define PCI_VENDOR_ID_REGISTER      0x00    // PCI Vendor ID Register
#define PCI_DEVICE_ID_REGISTER      0x02    // PCI Device ID Register
#define PCI_CONFIG_ID_REGISTER      0x00    // PCI Configuration ID Register
#define PCI_COMMAND_REGISTER        0x04    // PCI Command Register
#define PCI_STATUS_REGISTER         0x06    // PCI Status Register
#define PCI_REV_ID_REGISTER         0x08    // PCI Revision ID Register
#define PCI_CLASS_CODE_REGISTER     0x09    // PCI Class Code Register
#define PCI_CACHE_LINE_REGISTER     0x0C    // PCI Cache Line Register
#define PCI_LATENCY_TIMER           0x0D    // PCI Latency Timer Register
#define PCI_HEADER_TYPE             0x0E    // PCI Header Type Register
#define PCI_BIST_REGISTER           0x0F    // PCI Built-In SelfTest Register
#define PCI_BAR_0_REGISTER          0x10    // PCI Base Address Register 0
#define PCI_BAR_1_REGISTER          0x14    // PCI Base Address Register 1
#define PCI_BAR_2_REGISTER          0x18    // PCI Base Address Register 2
#define PCI_BAR_3_REGISTER          0x1C    // PCI Base Address Register 3
#define PCI_BAR_4_REGISTER          0x20    // PCI Base Address Register 4
#define PCI_BAR_5_REGISTER          0x24    // PCI Base Address Register 5
#define PCI_SUBVENDOR_ID_REGISTER   0x2C    // PCI SubVendor ID Register
#define PCI_SUBDEVICE_ID_REGISTER   0x2E    // PCI SubDevice ID Register
#define PCI_EXPANSION_ROM           0x30    // PCI Expansion ROM Base Register
#define PCI_INTERRUPT_LINE          0x3C    // PCI Interrupt Line Register
#define PCI_INTERRUPT_PIN           0x3D    // PCI Interrupt Pin Register
#define PCI_MIN_GNT_REGISTER        0x3E    // PCI Min-Gnt Register
#define PCI_MAX_LAT_REGISTER        0x3F    // PCI Max_Lat Register
#define PCI_NODE_ADDR_REGISTER      0x40    // PCI Node Address Register

#endif  // _RLTK_1G_H

