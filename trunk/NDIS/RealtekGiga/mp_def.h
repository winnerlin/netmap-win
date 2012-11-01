/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:
    mp_def.h

Abstract:
    NIC specific definitions

Revision History:

Notes:

--*/

#ifndef _MP_DEF_H
#define _MP_DEF_H

#include "linux_h.h"

// memory tag for this driver   
#define NIC_TAG                         ((ULONG)'XELA')
#define NIC_DBG_STRING                  (" ** Realtek1G ** \n") 

// packet and header sizes
#define NIC_MIN_PACKET_SIZE             60
#define NIC_HEADER_SIZE                 14

// multicast list size                          
#define NIC_MAX_MCAST_LIST              32

// update the driver version number every time you release a new driver
// The high word is the major version. The low word is the minor version.
// let's start with 6.0 for NDIS 6.0 driver
#define NIC_MAJOR_DRIVER_VERSION        0x06
#define NIC_MINOR_DRIVER_VERISON        0x00

// update the driver version number every time you release a new driver
// The high word is the major version. The low word is the minor version.
// this should be the same as the version reported in miniport driver characteristics

#define NIC_VENDOR_DRIVER_VERSION       ((NIC_MAJOR_DRIVER_VERSION << 16) | NIC_MINOR_DRIVER_VERISON)

// NDIS version in use by the NIC driver. 
// The high byte is the major version. The low byte is the minor version. 
#define NIC_DRIVER_VERSION              0x0600


// media type, we use ethernet, change if necessary
#define NIC_MEDIA_TYPE                  NdisMedium802_3


//
// maximum link speed for send dna recv in bps
//
#define NIC_MEDIA_MAX_SPEED             100000000

// interface type, we use PCI
#define NIC_INTERFACE_TYPE              NdisInterfacePci
#define NIC_INTERRUPT_MODE              NdisInterruptLevelSensitive 

// NIC PCI Device and vendor IDs 
#define NIC_PCI_VENDOR_ID               0x10EC	//  changed 0x8086
#define NIC_PCI_DEVICE_ID               0x8169	//  changed 0x1229
#define NIC_PCI_DEVICE_ID_2             0x8168	
#define NIC_PCI_DEVICE_ID_3             0x8111	
#define NIC_PCI_DEVICE_ID_4             0x8100
#define NIC_PCI_DEVICE_ID_5             0x8101

// buffer size passed in NdisMQueryAdapterResources                            
// We should only need three adapter resources (IO, interrupt and memory),
// Some devices get extra resources, so have room for 10 resources 
#define NIC_RESOURCE_BUF_SIZE           (sizeof(NDIS_RESOURCE_LIST) + \
                                        (10*sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR)))

// IO space length
#define NIC_MAP_IOSPACE_LENGTH          sizeof(CSR_STRUC)

// PCS config space including the Device Specific part of it/
#define NIC_PCI_RLTK_HDR_LENGTH         0xe2

// define some types for convenience
// TXCB_STRUC, RBUF_STRUC and CSR_STRUC are hardware specific structures

// hardware CSR (Control Status Register) structure                         
typedef CSR_STRUC                       HW_CSR;                                       
typedef PCSR_STRUC                      PHW_CSR;                                      

// change to your company name instead of using Microsoft
#define NIC_VENDOR_DESC                 "Driware"

// number of TBDs per processor - min, default and max
#define NIC_MIN_TBDS                    1
#define NIC_DEF_TBDS                    32
#define NIC_MAX_TBDS                    64

// max number of physical fragments supported per TBD
#define NIC_MAX_PHYS_BUF_COUNT        8    
#define	REALTEK_MAX_TBD					1024
// number of RECV_BUFFERs - min, default and max
#define NIC_MIN_RECV_BUFFERS                    4
#define NIC_DEF_RECV_BUFFERS                    20
#define NIC_MAX_RECV_BUFFERS                    1024

// only grow the RECV_BUFFERs up to this number
#define NIC_MAX_GROW_RECV_BUFFERS               128 

// How many intervals before the RECV_BUFFER list is shrinked?
#define NIC_RECV_BUFFER_SHRINK_THRESHOLD        10

// max lookahead size
#define NIC_MAX_LOOKAHEAD               (NIC_MAX_RECV_FRAME_SIZE - NIC_HEADER_SIZE)

// max number of send packets the MiniportSendPackets function can accept                            
#define NIC_MAX_SEND_PACKETS            10

// supported filters
#define NIC_SUPPORTED_FILTERS (     \
    NDIS_PACKET_TYPE_DIRECTED       | \
    NDIS_PACKET_TYPE_MULTICAST      | \
    NDIS_PACKET_TYPE_BROADCAST      | \
    NDIS_PACKET_TYPE_PROMISCUOUS    | \
    NDIS_PACKET_TYPE_ALL_MULTICAST)

// Threshold for a remove 
#define NIC_HARDWARE_ERROR_THRESHOLD    5

// The CheckForHang intervals before we decide the send is stuck
#define NIC_SEND_HANG_THRESHOLD         5        

// NDIS_ERROR_CODE_ADAPTER_NOT_FOUND                                                     
#define ERRLOG_READ_PCI_SLOT_FAILED     0x00000101L
#define ERRLOG_WRITE_PCI_SLOT_FAILED    0x00000102L
#define ERRLOG_VENDOR_DEVICE_NOMATCH    0x00000103L

// NDIS_ERROR_CODE_ADAPTER_DISABLED
#define ERRLOG_BUS_MASTER_DISABLED      0x00000201L

// NDIS_ERROR_CODE_UNSUPPORTED_CONFIGURATION
#define ERRLOG_INVALID_SPEED_DUPLEX     0x00000301L
#define ERRLOG_SET_SECONDARY_FAILED     0x00000302L

// NDIS_ERROR_CODE_OUT_OF_RESOURCES
#define ERRLOG_OUT_OF_MEMORY            0x00000401L
#define ERRLOG_OUT_OF_SHARED_MEMORY     0x00000402L
#define ERRLOG_OUT_OF_BUFFER_POOL       0x00000404L
#define ERRLOG_OUT_OF_NDIS_BUFFER       0x00000405L
#define ERRLOG_OUT_OF_PACKET_POOL       0x00000406L
#define ERRLOG_OUT_OF_NDIS_PACKET       0x00000407L
#define ERRLOG_OUT_OF_LOOKASIDE_MEMORY  0x00000408L
#define ERRLOG_OUT_OF_SG_RESOURCES      0x00000409L

// NDIS_ERROR_CODE_HARDWARE_FAILURE
#define ERRLOG_SELFTEST_FAILED          0x00000501L
#define ERRLOG_INITIALIZE_ADAPTER       0x00000502L
#define ERRLOG_REMOVE_MINIPORT          0x00000503L

// NDIS_ERROR_CODE_RESOURCE_CONFLICT
#define ERRLOG_MAP_IO_SPACE             0x00000601L
#define ERRLOG_QUERY_ADAPTER_RESOURCES  0x00000602L
#define ERRLOG_NO_IO_RESOURCE           0x00000603L
#define ERRLOG_NO_INTERRUPT_RESOURCE    0x00000604L
#define ERRLOG_NO_MEMORY_RESOURCE       0x00000605L

// NIC specific macros                                        
#define NIC_RBD_STATUS(_pMpRbd) _pMpRbd->HwRbd->status

#define NIC_RBD_STATUS_COMPLETED(_Status) \
	( !(_Status & OWNbit )  )

#define NIC_RBD_STATUS_SUCCESS(_Status) ((_Status) & RECV_BUFFER_STATUS_OK)

#define NIC_RBD_GET_PACKET_SIZE(pMpRbd) (pMpRbd->HwRbd->status & RECV_BUFFER_ACT_COUNT_MASK)

#define NIC_RBD_VALID_ACTUALCOUNT(pMpRbd) NIC_RBD_GET_PACKET_SIZE(pMpRbd)


// Constants for various purposes of NdisStallExecution

#define NIC_DELAY_POST_RESET            20

                                      
#endif  // _MP_DEF_H



