/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:
    mp_nic.h

Abstract:
    Function prototypes for mp_nic.c, mp_init.c and mp_req.c

Revision History:

Notes:

--*/

#ifndef _MP_NIC_H
#define _MP_NIC_H

#define		NIC_LINK_UP		(RTL_R8(PHYstatus) & LinkStatus)
#define		NIC_LINK_DOWN	(!(RTL_R8(PHYstatus) & LinkStatus))

#define NIC_INTERRUPT_DISABLED(_adapter) \
     ( ! ( RTL_R16( IntrMask ) & SCB_INT_MASK ) )

#define NIC_INTERRUPT_ACTIVE(_adapter) \
	( RTL_R16( IntrStatus ) & SCB_INT_MASK )

#define NIC_ACK_INTERRUPT(_adapter, _value) { \
	USHORT	u16 = RTL_R16( IntrStatus ) ; \
   _value = u16 & SCB_ACK_MASK; \
   RTL_W16( IntrStatus , _value ) ; }        

_inline BOOLEAN NIC_IS_RECV_READY( PMP_ADAPTER _adapter) { \
	ULONG_PTR ioaddr = _adapter->PortOffset ;
	return ( RTL_R8 ( ChipCmd ) & CmdRxEnb ) ; }
    // ((_adapter->CSRAddress->ScbStatus & SCB_RUS_MASK) == SCB_RUS_READY)
    
#define		NIC_ENABLE_RECEIVER			RTL_W8 ( ChipCmd , RTL_R8(ChipCmd) | CmdRxEnb ) ; 
#define		NIC_DISABLE_RECEIVER		RTL_W8 ( ChipCmd , RTL_R8(ChipCmd) & (~CmdRxEnb) ) ; 

__inline VOID NICDisableInterrupt(
    IN PMP_ADAPTER Adapter)
{
	ULONG_PTR ioaddr = Adapter->PortOffset ;
	RTL_W16( IntrMask, 0 ) ;
}
__inline VOID NIC_ENABLE_XMIT ( PMP_ADAPTER A ) \
{\
	ULONG_PTR ioaddr=A->PortOffset ; \
	/* enable Tx*/ \
	UCHAR tmp8 = RTL_R8( ChipCmd ); \
	RTL_W8 ( ChipCmd, tmp8 | CmdTxEnb); \
}

__inline VOID NIC_DISABLE_XMIT ( PMP_ADAPTER A ) \
{\
	ULONG_PTR ioaddr=A->PortOffset ; \
	/* disable Tx*/ \
	UCHAR tmp8 = RTL_R8( ChipCmd ); \
	RTL_W8 ( ChipCmd, tmp8 & (~CmdTxEnb) ); \
}
__inline VOID NIC_START_NORMAL_POLLING ( PMP_ADAPTER A ) \
{\
	ULONG_PTR ioaddr=A->PortOffset ; \
	/* set normal xmit polling bit */ \
	UCHAR tmp8 = RTL_R8( TxPoll ); \
	RTL_W8 ( TxPoll, tmp8 | 0x40); \
}

__inline VOID NICEnableInterrupt(
    IN PMP_ADAPTER Adapter)
{
	ULONG_PTR ioaddr = Adapter->PortOffset ;
	RTL_W16( IntrMask, SCB_INT_MASK ) ;
}
    

//
//  MP_NIC.C
//                    
NDIS_STATUS MiniportSendNetBufferList(
    IN  PMP_ADAPTER         Adapter,
    IN  PNET_BUFFER_LIST    NetBufferList,
    IN  BOOLEAN             bFromQueue);
   
NDIS_STATUS NICSendNetBuffer(
    IN  PMP_ADAPTER     Adapter,
    IN  PMP_TCB         pMpTcb,
    IN  PMP_FRAG_LIST   pFragList);
                                
ULONG MpCopyPacket(
    IN  PMDL            CurrMdl,
    IN  PMP_TXBUF       pMpTxbuf); 

ULONG MpCopyNetBuffer(
    IN  PNET_BUFFER     NetBuffer,
    IN  PMP_TXBUF       pMpTxbuf);

    
NDIS_STATUS NICStartSend(
    IN  PMP_ADAPTER     Adapter,
    IN  PMP_TCB         pMpTcb);
   
NDIS_STATUS MpHandleSendInterrupt(
    IN  PMP_ADAPTER     Adapter);
                   
VOID MpHandleRecvInterrupt(
    IN  PMP_ADAPTER     Adapter);
   
VOID NICReturnRBD(
    IN  PMP_ADAPTER     Adapter,
    IN  PMP_RBD         pMpRbd);

void
ZeroRBDandDisOwnToNIC ( 
		PHW_RBD pHwRbd,
		PMP_RBD pMpRbd
		);
   
NDIS_STATUS NICStartRecv(
    IN  PMP_ADAPTER     Adapter);

VOID MpFreeQueuedSendNetBufferLists(
    IN  PMP_ADAPTER     Adapter);

void MpFreeBusySendNetBufferLists(
    IN  PMP_ADAPTER     Adapter);
                            
void NICResetRecv(
    IN  PMP_ADAPTER     Adapter);

VOID 
MpHandleLinkChangeInterrupt(
				   PMP_ADAPTER         Adapter
				   );

VOID  MpHandleLinkUp ( PMP_ADAPTER         Adapter ) ;
VOID  MpHandleLinkDown ( PMP_ADAPTER         Adapter ) ;

VOID
MPIndicateLinkState(
    IN  PMP_ADAPTER     Adapter
    );

//
// MP_INIT.C
//                  
      
NDIS_STATUS 
MpFindAdapter(
    IN  PMP_ADAPTER             Adapter,
    IN  PNDIS_RESOURCE_LIST     resList
    );

NDIS_STATUS NICReadAdapterInfo(
    IN  PMP_ADAPTER     Adapter);
              
NDIS_STATUS MpAllocAdapterBlock(
    OUT PMP_ADAPTER    *pAdapter,
    IN  NDIS_HANDLE     MiniportAdapterHandle
    );
    
void MpFreeAdapter(
    IN  PMP_ADAPTER     Adapter);
                                         
NDIS_STATUS NICReadRegParameters(
    IN  PMP_ADAPTER     Adapter);
                                              
NDIS_STATUS NICAllocAdapterMemory(
    IN  PMP_ADAPTER     Adapter);
   
VOID NICInitSend(
    IN  PMP_ADAPTER     Adapter);

NDIS_STATUS NICInitRecv(
    IN  PMP_ADAPTER     Adapter);

ULONG NICAllocRBD(
    IN  PMP_ADAPTER     Adapter, 
    IN  PMP_RBD         pMpRbd);

void
SetupRbd ( PMP_ADAPTER Adapter, PMP_RBD pMpRbd );

ULONG
FindAGapInRecvBuffers (
    IN  PMP_ADAPTER  Adapter
	);

VOID NICFreeRecvBuffer(
    IN  PMP_ADAPTER     Adapter, 
    IN  PMP_RBD         pMpRbd,
    IN  BOOLEAN         DispatchLevel);
   
NDIS_STATUS NICSelfTest(
    IN  PMP_ADAPTER     Adapter);

NDIS_STATUS NICconfigureAndInitializeAdapter(
    IN  PMP_ADAPTER     Adapter);

 HwSoftwareReset(
    IN  PMP_ADAPTER     Adapter);

NDIS_STATUS HwConfigure(
    IN  PMP_ADAPTER     Adapter);

// //new additions for realtek driver
int rtl8169_init_one ( IN  PMP_ADAPTER     Adapter );
int r1000_init_one ( PMP_ADAPTER Adapter ) ;

NDIS_STATUS HwSetupIAAddress(
    IN  PMP_ADAPTER     Adapter);

NDIS_STATUS HwClearAllCounters(
    IN  PMP_ADAPTER     Adapter);

//
// MP_REQ.C
//                  
    
NDIS_STATUS NICGetStatsCounters(
    IN  PMP_ADAPTER     Adapter, 
    IN  NDIS_OID        Oid,
    OUT PULONG64        pCounter);
    
NDIS_STATUS NICSetPacketFilter(
    IN  PMP_ADAPTER     Adapter,
    IN  ULONG           PacketFilter);

NDIS_STATUS NICSetMulticastList(
    IN  PMP_ADAPTER     Adapter);
    
ULONG NICGetMediaConnectStatus(
    IN  PMP_ADAPTER     Adapter);
    
VOID
MPFillPoMgmtCaps (
    IN PMP_ADAPTER Adapter, 
    IN OUT PNDIS_PNP_CAPABILITIES   pPower_Management_Capabilities, 
    IN OUT  PNDIS_STATUS pStatus,
    IN OUT  PULONG pulInfoLen
    );

                    
#endif  // MP_NIC_H

