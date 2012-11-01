/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:
    mp_nic.c

Abstract:
    This module contains miniport send/receive routines

Revision History:

Notes:

--*/

#include "precomp.h"
#include <xfilter.h>
#if DBG
#define _FILENUMBER     'CINM'
#endif

__inline 
MP_SEND_STATS(
    IN PMP_ADAPTER  Adapter,
    IN PNET_BUFFER  NetBuffer
    )
/*++
Routine Description:
    This function updates the send statistics on the Adapter.
    This is called only after a NetBuffer has been sent out. 

    These should be maintained in hardware to increase performance.
    They are demonstrated here as all NDIS 6.0 miniports are required
    to support these statistics.

Arguments:

    Adapter     Pointer to our adapter
    NetBuffer   Pointer the the sent NetBuffer
Return Value:
    None

--*/
{
    PUCHAR  EthHeader;
    ULONG   Length;
    PMDL    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);

    NdisQueryMdl(Mdl, &EthHeader, &Length, NormalPagePriority);
    if (EthHeader != NULL)
    {
        EthHeader += NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
        if (ETH_IS_BROADCAST(EthHeader))
        {
            Adapter->OutBroadcastPkts++;
            Adapter->OutBroadcastOctets +=NET_BUFFER_DATA_LENGTH(NetBuffer);
        }
        else if (ETH_IS_MULTICAST(EthHeader))
        {
            Adapter->OutMulticastPkts++;
            Adapter->OutMulticastOctets +=NET_BUFFER_DATA_LENGTH(NetBuffer);
        }
        else
        {
            Adapter->OutUcastPkts++;
            Adapter->OutUcastOctets += NET_BUFFER_DATA_LENGTH(NetBuffer);
        }
    }

}

__inline 
MP_RECEIVE_STATS(
    IN PMP_ADAPTER  Adapter,
    IN PNET_BUFFER  NetBuffer
    )
/*++
Routine Description:
    This function updates the receive statistics on the Adapter.
    This is incremented before the NetBufferList is indicated to NDIS. 

    These should be maintained in hardware to increase performance.
    They are demonstrated here as all NDIS 6.0 miniports are required
    to support these statistics.

Arguments:

    Adapter     Pointer to our adapter
    NetBuffer   Pointer the the sent NetBuffer
Return Value:
    None

--*/
{
    PUCHAR  EthHeader;
    ULONG   Length;
    PMDL    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG   NbLength = NET_BUFFER_DATA_LENGTH(NetBuffer);

    NdisQueryMdl(Mdl, &EthHeader, &Length, NormalPagePriority);
    EthHeader += NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    if (ETH_IS_BROADCAST(EthHeader))
    {
        Adapter->InBroadcastPkts++;
        Adapter->InBroadcastOctets +=NbLength;
    }
    else if (ETH_IS_MULTICAST(EthHeader))
    {
        Adapter->InMulticastPkts++;
        Adapter->InMulticastOctets +=NbLength;
    }
    else
    {
        Adapter->InUcastPkts++;
        Adapter->InUcastOctets += NbLength;
    }

}

__inline PNET_BUFFER_LIST 
MP_FREE_SEND_NET_BUFFER(
    IN  PMP_ADAPTER Adapter,
    IN  PMP_TCB     pMpTcb
    )
/*++
Routine Description:

    Recycle a MP_TCB and complete the packet if necessary
    Assumption: Send spinlock has been acquired 

Arguments:

    Adapter     Pointer to our adapter
    pMpTcb      Pointer to MP_TCB        

Return Value:

    Return NULL if no NET_BUFFER_LIST is completed.
    Otherwise, return a pointer to a NET_BUFFER_LIST which has been completed.

--*/
{
    
    PNET_BUFFER         NetBuffer;
    PNET_BUFFER_LIST    NetBufferList;
    BOOLEAN             fWaitForMapping = FALSE;

    ASSERT(MP_TEST_FLAG(pMpTcb, fMP_TCB_IN_USE));
    
    fWaitForMapping = MP_TEST_FLAG(pMpTcb, fMP_TCB_WAIT_FOR_MAPPING);
    
    NetBuffer = pMpTcb->NetBuffer;
    NetBufferList = pMpTcb->NetBufferList;
    pMpTcb->NetBuffer = NULL;
    pMpTcb->Count = 0;

    // Update the list before releasing the Spin-Lock 
    // to avoid race condition on the Adapter send list
    Adapter->CurrSendHead = Adapter->CurrSendHead->Next;
    Adapter->nBusySend--;
    MP_CLEAR_FLAGS(pMpTcb);
    ASSERT(Adapter->nBusySend >= 0);

    if (NetBuffer && !fWaitForMapping)
    {
        //
        // Call ndis to free resouces allocated for a SG list
        //
        NdisDprReleaseSpinLock(&Adapter->SendLock);

        NdisMFreeNetBufferSGList(
                            Adapter->NdisMiniportDmaHandle,
                            pMpTcb->pSGList,
                            NetBuffer);
        NdisDprAcquireSpinLock(&Adapter->SendLock);
    }
    
    //
    // SendLock is hold
    //
    if (NetBufferList)
    {
        MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList)--;
        if (MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList) == 0)
        {
            DBGPRINT(MP_TRACE, ("Completing NetBufferList= "PTR_FORMAT"\n", NetBufferList));
            NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
            return NetBufferList;
        }
    }

    return NULL;
    
}

///new additions for realtek driver 
// assumption : send spinlock held
_inline 
PTBD_STRUC
INC_TBD ( PMP_ADAPTER A,
		 PTBD_STRUC	pHwTbd 
		 )
{
	PTBD_STRUC base, top ;
	ULONG nTbds = A->nTbds ;
	
	base = A->HwTbdBase ;
	top = base + nTbds ;

	++pHwTbd;
	if ( pHwTbd >= top )
		return base ;
	else
		return pHwTbd ;
}

////new additions for realtek driver
// assumption : send spinlock held
VOID
InitializeTBD ( 
			   PTBD_STRUC	pHwTbd 
			   )
{
	USHORT	saveEORbit ;

	saveEORbit = pHwTbd->status & 0x4000 ;  // (USHORT)(EORbit>>16) ;
	NdisZeroMemory ( pHwTbd, sizeof(TBD_STRUC) );
	pHwTbd->status |= saveEORbit ;
}

////new additions for realtek driver
_inline
PTBD_STRUC
GetNextFreeTBD ( PMP_ADAPTER A 
				)
{
	PTBD_STRUC pHwTbd  ;

	pHwTbd = A->NextFreeTbd ;
	//A->NextFreeTbd = INC_TBD ( A, pHwTbd ) ;

	InitializeTBD ( pHwTbd ) ;
	return pHwTbd ;

}

NDIS_STATUS
MiniportSendNetBufferList(
    IN  PMP_ADAPTER         Adapter,
    IN  PNET_BUFFER_LIST    NetBufferList,
    IN  BOOLEAN             bFromQueue
    )
/*++
Routine Description:

    Do the work to send a packet
    Assumption: Send spinlock has been acquired 

Arguments:

    Adapter             Pointer to our adapter
    NetBufferList       Pointer to the NetBufferList is going to send.
    bFromQueue          TRUE if it's taken from the send wait queue

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_PENDING         Put into the send wait queue
    NDIS_STATUS_HARD_ERRORS

    NOTE: SendLock is held, called at DISPATCH level
--*/
{
    NDIS_STATUS     Status = NDIS_STATUS_PENDING;
    NDIS_STATUS     SendStatus;
    PMP_TCB         pMpTcb = NULL;
    PMP_TXBUF       pMpTxBuf = NULL;
    ULONG           BytesCopied;
    PNET_BUFFER     NetBufferToSend;
    BOOLEAN         bSendNetBuffer = FALSE;
    
    // State
    PMP_TCB pMpTcbState = NULL; 
    USHORT CbStatusState;

    DBGPRINT(MP_TRACE, ("--> MiniportSendNetBufferList, NetBufferList="PTR_FORMAT"\n", 
                            NetBufferList));
    
    SendStatus = NDIS_STATUS_SUCCESS;

    if (bFromQueue)
    {
        NetBufferToSend = MP_GET_NET_BUFFER_LIST_NEXT_SEND(NetBufferList);
        MP_GET_NET_BUFFER_LIST_NEXT_SEND(NetBufferList) = NULL;
    }
    else
    {
        NetBufferToSend = NET_BUFFER_LIST_FIRST_NB(NetBufferList);
    }
    for (;  NetBufferToSend != NULL;
            NetBufferToSend = NET_BUFFER_NEXT_NB(NetBufferToSend))
    {
        bSendNetBuffer = FALSE;

        //
        // If we run out of TCB
        // 
        if (!MP_TCB_RESOURCES_AVAIABLE(Adapter))
        {
            ASSERT(NetBufferToSend != NULL);
            //
            // Put NetBufferList into wait queue
            //
            MP_GET_NET_BUFFER_LIST_NEXT_SEND(NetBufferList) = NetBufferToSend;
            if (!bFromQueue)
            {
                InsertHeadQueue(&Adapter->SendWaitQueue,
                                MP_GET_NET_BUFFER_LIST_LINK(NetBufferList));
                Adapter->nWaitSend++;
                
            }
            //
            // The NetBufferList is already in the queue, we don't do anything
            //
            Adapter->SendingNetBufferList = NULL;
            Status = NDIS_STATUS_RESOURCES;
            break;
        }
        //
        // Get TCB
        //
        pMpTcb = Adapter->CurrSendTail;
        ASSERT(!MP_TEST_FLAG(pMpTcb, fMP_TCB_IN_USE));
        
        
        pMpTcb->Adapter = Adapter;
        pMpTcb->NetBuffer = NetBufferToSend;
        pMpTcb->NetBufferList = NetBufferList;


		//new additions for realtek driver
		// this is where MP_TCB is baptized
		// use this place to init HW TBD also
		pMpTcb->TbdStart = GetNextFreeTBD ( Adapter ) ;
		pMpTcb->nTbds = 0 ;
    
        // Update the list before releasing the Spin-Lock 
        // to avoid race condition on the Adapter send list
        // Save the state
        pMpTcbState = Adapter->CurrSendTail;

		// Do the Updates
        Adapter->nBusySend++;
        ASSERT(Adapter->nBusySend <= Adapter->NumTcb);
        Adapter->CurrSendTail = Adapter->CurrSendTail->Next;

        {
            ASSERT(MP_TEST_FLAG(Adapter, fMP_ADAPTER_SCATTER_GATHER));
            //
            // NOTE: net Buffer has to use new DMA APIs
            // 
            //
            // The function is called at DISPATCH level
            //
            MP_SET_FLAG(pMpTcb, fMP_TCB_IN_USE);
            MP_SET_FLAG(pMpTcb, fMP_TCB_WAIT_FOR_MAPPING);
            
            MP_RELEASE_SPIN_LOCK(&Adapter->SendLock, TRUE);

            SendStatus = NdisMAllocateNetBufferSGList(
                            Adapter->NdisMiniportDmaHandle,
                            NetBufferToSend,
                            pMpTcb,
                            NDIS_SG_LIST_WRITE_TO_DEVICE,
                            pMpTcb->ScatterGatherListBuffer,
                            Adapter->ScatterGatherListSize);

            MP_ACQUIRE_SPIN_LOCK(&Adapter->SendLock, TRUE);            
            
            if (NDIS_STATUS_SUCCESS != SendStatus)
            {
                DBGPRINT(MP_WARN, ("NetBuffer can't be mapped, NetBuffer= "PTR_FORMAT"\n", NetBufferToSend));

                // Revert the State
                Adapter->CurrSendTail = pMpTcbState ;
                Adapter->nBusySend--;

                MP_CLEAR_FLAGS(pMpTcb);
                pMpTcb->NetBuffer = NULL;
                pMpTcb->NetBufferList = NULL;
                // 
                // We should fail the entire NET_BUFFER_LIST because the system 
                // cannot map the NET_BUFFER
                //
                NET_BUFFER_LIST_STATUS(NetBufferList) = NDIS_STATUS_RESOURCES;
                
                for (; NetBufferToSend != NULL;
                       NetBufferToSend = NET_BUFFER_NEXT_NB(NetBufferToSend))
                {
                    MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList)--;
                }
                       
                break;
            }
        }
    }
    //
    // All the NetBuffers in the NetBufferList has been processed,
    // If the NetBufferList is in queue now, dequeue it.
    //
    if (NetBufferToSend == NULL)
    {
        if (bFromQueue)
        {
            RemoveHeadQueue(&Adapter->SendWaitQueue);
            Adapter->nWaitSend--;
        }
        Adapter->SendingNetBufferList = NULL;
    }

    //
    // As far as the miniport knows, the NetBufferList has been sent out.
    // Complete the NetBufferList now
    //
    if ((MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList) == 0)&&(NDIS_STATUS_SUCCESS != SendStatus))
    {
        
        MP_RELEASE_SPIN_LOCK(&Adapter->SendLock, TRUE);

        NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
        NdisMSendNetBufferListsComplete(
            MP_GET_ADAPTER_HANDLE(Adapter),
            NetBufferList,
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);   
        MP_ACQUIRE_SPIN_LOCK(&Adapter->SendLock, TRUE);
    } 
    
    DBGPRINT(MP_TRACE, ("<-- MiniportSendNetBufferList\n"));
    return Status;

}  
/*++
Routine Description:
    
   Copy data in a packet to the specified location 
    
Arguments:
    
    BytesToCopy          The number of bytes need to copy
    CurreentBuffer       The buffer to start to copy
    StartVa              The start address to copy the data to
    Offset               The start offset in the buffer to copy the data

Return Value:
 
    The number of bytes actually copied
  

--*/  

ULONG 
MpCopyNetBuffer(
    IN  PNET_BUFFER     NetBuffer, 
    IN  PMP_TXBUF       pMpTxBuf
    )
{
    ULONG          CurrLength;
    PUCHAR         pSrc;
    PUCHAR         pDest;
    ULONG          BytesCopied = 0;
    ULONG          Offset;
    PMDL           CurrentMdl;
    ULONG          DataLength;
    
    DBGPRINT(MP_TRACE, ("--> MpCopyNetBuffer\n"));
    
    pDest = pMpTxBuf->pBuffer;
    CurrentMdl = NET_BUFFER_FIRST_MDL(NetBuffer);
    Offset = NET_BUFFER_DATA_OFFSET(NetBuffer);
    DataLength = NET_BUFFER_DATA_LENGTH(NetBuffer);
    
    while (CurrentMdl && DataLength > 0)
    {
        NdisQueryMdl(CurrentMdl, &pSrc, &CurrLength, NormalPagePriority);
        if (pSrc == NULL)
        {
            BytesCopied = 0;
            break;
        }
        // 
        //  Current buffer length is greater than the offset to the buffer
        //  
        if (CurrLength > Offset)
        { 
            pSrc += Offset;
            CurrLength -= Offset;

            if (CurrLength > DataLength)
            {
                CurrLength = DataLength;
            }
            DataLength -= CurrLength;
            NdisMoveMemory(pDest, pSrc, CurrLength);
            BytesCopied += CurrLength;

            pDest += CurrLength;
            Offset = 0;
        }
        else
        {
            Offset -= CurrLength;
        }
        NdisGetNextMdl(CurrentMdl, &CurrentMdl);
    
    }
    if ((BytesCopied != 0) && (BytesCopied < NIC_MIN_PACKET_SIZE))
    {
        NdisZeroMemory(pDest, NIC_MIN_PACKET_SIZE - BytesCopied);
    }
    NdisAdjustMdlLength(pMpTxBuf->Mdl, BytesCopied);
    NdisFlushBuffer(pMpTxBuf->Mdl, TRUE);
    
    ASSERT(BytesCopied <= pMpTxBuf->BufferSize);
    DBGPRINT(MP_TRACE, ("<-- MpCopyNetBuffer\n"));
    return BytesCopied;
}


NDIS_STATUS 
NICSendNetBuffer(
    IN  PMP_ADAPTER     Adapter,
    IN  PMP_TCB         pMpTcb,
    IN  PMP_FRAG_LIST   pFragList
    )
/*++
Routine Description:

    NIC specific send handler
    Assumption: Send spinlock has been acquired 

Arguments:

    Adapter     Pointer to our adapter
    pMpTcb      Pointer to MP_TCB
    pFragList   The pointer to the frag list to be filled

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_HARD_ERRORS
NOTE: called with send lock held.    

--*/
{
    NDIS_STATUS  Status;
    ULONG        index;
    UCHAR        nTbds = 0;
    PNET_BUFFER  NetBuffer;
	ULONGLONG    DataOffset;

	union both {
		UINT64		A64bitX ;
		UINT32		AsInt32[2] ;
	} both ;

	PTBD_STRUC   pHwTbd = pMpTcb->TbdStart ;

    DBGPRINT(MP_TRACE, ("--> NICSendNetBuffer\n"));
    NetBuffer = pMpTcb->NetBuffer;
    
    //
    // This one is using the local buffer
    //
    if (MP_TEST_FLAG(pMpTcb, fMP_TCB_USE_LOCAL_BUF))
    {
        for (index = 0; index < pFragList->NumberOfElements; index++)
        {
            if (pFragList->Elements[index].Length)
            {
                //set TBD buffer address
				pHwTbd->TbdBufferAddress = 
					NdisGetPhysicalAddressLow(pFragList->Elements[index].Address);        
				pHwTbd->TbdBufferAddressHigh = 
					NdisGetPhysicalAddressHigh(pFragList->Elements[index].Address);         ;

				// set frame length
                pHwTbd->FrameLength = (USHORT)pFragList->Elements[index].Length;

				// proceed to configure next TBD
				pHwTbd = INC_TBD ( Adapter, pHwTbd ) ;
                nTbds++;
            }
        }
    }
    else
    {
        //
        // NDIS starts creating SG list from CurrentMdl, the driver don't need to worry
        // about more data will be mapped because NDIS will only map the data to length of
        // (NetBuffer->DataLength + NetBuffer->CurrentMdlOffset).The driver only need to skip 
        // the offset.
        //
        DataOffset = (NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer)); 
                
        for (index = 0; index < pFragList->NumberOfElements; index++)
        {
            
            if (pFragList->Elements[index].Length <= DataOffset)
            {
                //
                // skip SG elements that are not used to get to the beginning of the data
                //
                DataOffset -= pFragList->Elements[index].Length;
            }
            else
            {
                //set TBD buffer address
				both.A64bitX = pFragList->Elements[index].Address.QuadPart ;
				both.A64bitX += DataOffset ;

				pHwTbd->TbdBufferAddress = both.AsInt32[0] ; 
				pHwTbd->TbdBufferAddressHigh = both.AsInt32[1] ; 

				// set frame length
                pHwTbd->FrameLength = 
					(USHORT)(pFragList->Elements[index].Length - DataOffset) ;

				// proceed to configure next TFD
				pHwTbd = INC_TBD ( Adapter, pHwTbd ) ;
                nTbds++;
				index++ ;

                break;
            }
        }

        for (; index < pFragList->NumberOfElements; index++)
        {
            if (pFragList->Elements[index].Length)
            {
                //set TBD buffer address
				pHwTbd->TbdBufferAddress = 
					NdisGetPhysicalAddressLow(pFragList->Elements[index].Address);        
				pHwTbd->TbdBufferAddressHigh = 
					NdisGetPhysicalAddressHigh(pFragList->Elements[index].Address);;

				// set frame length
                pHwTbd->FrameLength = (USHORT)pFragList->Elements[index].Length;

				// proceed to configure next TBD
				pHwTbd = INC_TBD ( Adapter, pHwTbd ) ;

                nTbds++;

                if ( nTbds > NIC_MAX_PHYS_BUF_COUNT )
                {
                    ASSERT(FALSE);
                }
            }
        }                
    }

	pMpTcb->nTbds = nTbds ;

    // //new additions for realtek driver
	if ( nTbds )
		Status = NICStartSend(Adapter, pMpTcb);
	else	{
		DBGPRINT( MP_WARN, ("strange!! SG fraGlist hosed?\n"));
		KdBreakPoint();
		Status = NDIS_STATUS_FAILURE ;

		// TBD here -- ALX
		// somehow we have to reclaim this net buffer and/or
		// complete the whole NBL w/ error status
		// otherwise this NB and corresponding NBL are in limbo as of now
		// NDIS may wait eternally for this NBL to complete
	}
    
    DBGPRINT(MP_TRACE, ("<-- NICSendNetBuffer\n"));

    return Status;
}

NDIS_STATUS 
NICStartSend(
    IN  PMP_ADAPTER  Adapter,
    IN  PMP_TCB      pMpTcb
    )
/*++

Routine Description:

    Issue a send command to the NIC
    Assumption: Send spinlock has been acquired 

Arguments:

    Adapter     Pointer to our adapter
    pMpTcb      Pointer to MP_TCB

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_HARD_ERRORS

--*/
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;

    DBGPRINT(MP_TRACE, ("--> NICStartSend\n"));

    //
	//
    // Setup RBD FS/LS/OWN bits. EOR NOT touched being set initially only.
	// Receive Buffer physical address and frame length 
	// fields in RBDs are already setup.
	// Finally start the transmitter by setting the XMIT enable bit in 
	// device register 0x37 and TPPOLL bit 6 xmit-polling bit in register 0x38
    // 
    //
    do
    {
        DBGPRINT(MP_LOUD, ("adding TBD to Active chain\n"));
       
		MP_CLEAR_FLAG(pMpTcb, fMP_TCB_WAIT_FOR_MAPPING);

		//
        // Issue the send command.
        //
		{
			ULONG i ;
			PTBD_STRUC pHwTbd ;

			// set LSbit & FSbit bits in this loop 
			for (	pHwTbd = pMpTcb->TbdStart,i=0 ;
					i<pMpTcb->nTbds; 
					++i ) 	{
				
				pHwTbd->status &= (~(FSbit));
				pHwTbd->status &= (~(LSbit));
					
				// set FIRST SEGMENT bit
				if ( i == 0 )	{
					pHwTbd->status |= FSbit ;
				}
				
				// set LAST SEGMENT bit
				if ( pMpTcb->nTbds && i == pMpTcb->nTbds -1 )	{
					pHwTbd->status |= (LSbit) ;
					pMpTcb->TbdEnd = pHwTbd ;
				}
				//set OWN bit
				pHwTbd->status |= (USHORT)(OWNbit>>16) ;

				// get next TBD
				pHwTbd = INC_TBD ( Adapter, pHwTbd ) ; 

				Adapter->NextFreeTbd = pHwTbd ;
			}
		}

        RealtekHwCommand(Adapter, SCB_XMIT_RESUME, FALSE );
    }
    while (FALSE);
    
    DBGPRINT(MP_TRACE, ("<-- NICStartSend\n"));

    return Status;
}

NDIS_STATUS
MpHandleSendInterrupt(
    IN  PMP_ADAPTER  Adapter
    )
/*++
Routine Description:

    Interrupt handler for sending processing
    Re-claim the send resources, complete sends and get more to send from the send wait queue
    Assumption: Send spinlock has been acquired 

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_HARD_ERRORS
    NDIS_STATUS_PENDING

--*/
{
    NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;
    PMP_TCB             pMpTcb;
    PNET_BUFFER_LIST    NetBufferList;
    PNET_BUFFER_LIST    LastNetBufferList = NULL;
    PNET_BUFFER_LIST    CompleteNetBufferLists = NULL;
	ULONG_PTR			ioaddr=Adapter->PortOffset;

#if DBG
    LONG            i;
#endif

    DBGPRINT(MP_TRACE, ("---> MpHandleSendInterrupt\n"));

    //
    // Any packets being sent? Any packet waiting in the send queue?
    //
    if (Adapter->nBusySend == 0 &&
        IsQueueEmpty(&Adapter->SendWaitQueue))
    {
        ASSERT(Adapter->CurrSendHead == Adapter->CurrSendTail);
        DBGPRINT(MP_TRACE, ("<--- MpHandleSendInterrupt\n"));
        return Status;
    }

    //
    // Check the first TBD on the send list
    //
    while (Adapter->nBusySend > 0)
    {
 #if DBG
        pMpTcb = Adapter->CurrSendHead;
        for (i = 0; i < Adapter->nBusySend; i++)
        {
            pMpTcb = pMpTcb->Next;   
        }

        if (pMpTcb != Adapter->CurrSendTail)
        {
            DBGPRINT(MP_ERROR, ("nBusySend= %d\n", Adapter->nBusySend));
            DBGPRINT(MP_ERROR, ("CurrSendhead= "PTR_FORMAT"\n", Adapter->CurrSendHead));
            DBGPRINT(MP_ERROR, ("CurrSendTail= "PTR_FORMAT"\n", Adapter->CurrSendTail));

            ASSERT(FALSE);
        }

#endif      

		pMpTcb = Adapter->CurrSendHead;

        //
        // Is this TBD completed?
        //
		if( pMpTcb->nTbds &&
				pMpTcb->TbdEnd &&
					(!(pMpTcb->TbdEnd->status & 0x8000)) // own bit back to cpu ?
		) 
        {
#if DBG
			Adapter->LastTbdSentByNic = pMpTcb->TbdEnd ;
#endif
				////new additions for realtek driver
				// if (pMpTcb->HwTbd->TxCbHeader.CbCommand & CB_CMD_MASK) != CB_MULTICAST)
            {
#ifdef DBG_SEND_INTERRUPT
		        DBGPRINT(MP_INFO, ("MPHandleSendInterrupt DPC :: VALID packet sent by TX unit\n"));
#endif
                //
                // Update the outgoing statistics
                //
                MP_SEND_STATS(Adapter, pMpTcb->NetBuffer);
                //
                // To change this to complete a list of NET_BUFFER_LIST
                //
                NetBufferList = MP_FREE_SEND_NET_BUFFER(Adapter, pMpTcb);
                //
                // One NetBufferList got complete
                //
                if (NetBufferList != NULL)
                {
                    NET_BUFFER_LIST_STATUS(NetBufferList) = NDIS_STATUS_SUCCESS;
                    if (CompleteNetBufferLists == NULL)
                    {
                        CompleteNetBufferLists = NetBufferList;
                    }
                    else
                    {
                        NET_BUFFER_LIST_NEXT_NBL(LastNetBufferList) = NetBufferList;
                    }
                    NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;                    
                    LastNetBufferList = NetBufferList;
                }
                
            }
        }
        else
        {
            break;
        }
    }
    //
    // Complete the NET_BUFFER_LISTs 
    //
    if (CompleteNetBufferLists != NULL)
    {
        NDIS_HANDLE MiniportHandle = MP_GET_ADAPTER_HANDLE(Adapter);           
        NdisDprReleaseSpinLock(&Adapter->SendLock);
        
        NdisMSendNetBufferListsComplete(
                MiniportHandle,
                CompleteNetBufferLists,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
        NdisDprAcquireSpinLock(&Adapter->SendLock);
        
    }

    //
    // If we queued any transmits because we didn't have any TBDs earlier,
    // dequeue and send those packets now, as long as we have free TBDs.
    //
    if (MP_IS_READY(Adapter))
    {
        while (!IsQueueEmpty(&Adapter->SendWaitQueue) &&
            (MP_TCB_RESOURCES_AVAIABLE(Adapter) &&
            Adapter->SendingNetBufferList == NULL))
            
        {
            PQUEUE_ENTRY pEntry; 
            
            //
            // We cannot remove it now, we just need to get the head
            //
            pEntry = GetHeadQueue(&Adapter->SendWaitQueue);
            ASSERT(pEntry);
            NetBufferList = MP_GET_NET_BUFFER_LIST_FROM_QUEUE_LINK (pEntry);

            DBGPRINT(MP_INFO, ("MpHandleSendInterrupt - send a queued NetBufferList\n"));
            
            ASSERT(Adapter->SendingNetBufferList == NULL);
            Adapter->SendingNetBufferList = NetBufferList;
            
            Status = MiniportSendNetBufferList(Adapter, NetBufferList, TRUE);
            //
            // If we failed to send
            // 
            if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_PENDING)
            {
                break;
            }
        }
    }

    
    DBGPRINT(MP_TRACE, ("<--- MpHandleSendInterrupt\n"));
    return Status;
}

BOOLEAN AllocateIoWorkItem = FALSE;
VOID
MPQueuedWorkItem(
    IN PVOID                        WorkItemContext,
    IN NDIS_HANDLE                  NdisIoWorkItemHandle
    );

// Currently this works w/o interlocked functions because
// recv lock is always held when this is invoked
// otherwise hosed
void
IncCurrentRBD ( PMP_ADAPTER A ) 
{
	PHW_RBD     pHwRbd = NULL ;
	int CurrentRBD;

	//temp quick code  for testing. not multi-thread safe  **/
	CurrentRBD =  A->CurrentRBD  ;
	
	if ( CurrentRBD+1 == A->NumHwRecvBuffers )	{
		A->CurrentRBD = 0 ;  // InterlockedExchange ( &A->CurrentRBD, 0 ) ;
		return ;
	}
	
	pHwRbd = (PHW_RBD)( A->HwRbdBase ) ;  // Get first RECV_BUFFER
	pHwRbd += CurrentRBD ;


	if ( pHwRbd->status & EORbit ) {
		A->CurrentRBD = 0 ; // InterlockedExchange ( &A->CurrentRBD, 0 ) ;
	}
	else
	{
		++A->CurrentRBD  ; // InterlockedIncrement ( &A->CurrentRBD ) ;
	}

	return ;

	/*** orifinal code. yet to debug ****/
	CurrentRBD = InterlockedCompareExchange ( &A->CurrentRBD, 0, A->NumHwRecvBuffers ) ;

	if ( CurrentRBD == A->NumHwRecvBuffers )
		return ;

	pHwRbd = (PHW_RBD)( A->HwRbdBase ) ;  // Get first RECV_BUFFER
	pHwRbd += CurrentRBD ;


	if ( pHwRbd->status & EORbit ) {
		InterlockedExchange ( &A->CurrentRBD, 0 ) ;
	}
	else
	{
		InterlockedIncrement ( &A->CurrentRBD ) ;
	}

}


VOID 
MpHandleRecvInterrupt(
    IN  PMP_ADAPTER  Adapter
    )
/*++
Routine Description:

    Interrupt handler for receive processing
    Put the received packets into an array and call NdisMIndicateReceivePacket
    If we run low on RECV_BUFFERs, allocate another one
    Assumption: Rcv spinlock has been acquired 

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    None
    
--*/
{
    PMP_RBD             pMpRbd;

    PNET_BUFFER_LIST    FreeNetBufferList;
    UINT                NetBufferListCount;
    UINT                NetBufferListFreeCount;
    UINT                Index;
    UINT                LoopIndex = 0;
    UINT                LoopCount = NIC_MAX_RECV_BUFFERS / NIC_DEF_RECV_BUFFERS + 1;    // avoid staying here too long

    BOOLEAN             bContinue = TRUE;
    BOOLEAN             bAllocNewRecvBuffer = FALSE;
    ULONG	            PacketStatus;
    NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;
    PNET_BUFFER_LIST    NetBufferList = NULL; 
    PNET_BUFFER_LIST    PrevNetBufferList = NULL;
    PNET_BUFFER_LIST    PrevFreeNetBufferList = NULL;
    ULONG               PendingRecv;
    PNET_BUFFER         NetBuffer;
    LONG                Count;
    ULONG               ReceiveFlags = NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;
	int					CurrentRBD = 0 ;
	NDIS_STATUS			status ;
	UINT				PacketSize ; 
	ULONG_PTR			ioaddr = Adapter->PortOffset ;


    DBGPRINT(MP_TRACE, ("---> MpHandleRecvInterrupt\n"));

    //
    // If NDIS is pausing the miniport or miniport is paused
    // IGNORE the recvs
    // 
    if ((Adapter->AdapterState == NicPausing) ||
        (Adapter->AdapterState == NicPaused))
    {
        return;
    }

    //
    // add an extra receive ref just in case we indicte up with status resources
    //
    MP_INC_RCV_REF(Adapter);
    
    ASSERT(Adapter->nReadyRecv >= NIC_MIN_RECV_BUFFERS);
    
    while (LoopIndex++ < LoopCount && bContinue)
    {
	
        NetBufferListCount = 0;
        NetBufferListFreeCount = 0;

        NetBufferList = NULL;
        FreeNetBufferList = NULL;

        // resetting the receive flags for each cycle
        ReceiveFlags = NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;

        //
        // Process up to the array size RECV_BUFFER's
        //
        while (NetBufferListCount < NIC_DEF_RECV_BUFFERS)
        {
            if (IsListEmpty(&Adapter->RecvList))
            {
                ASSERT(Adapter->nReadyRecv == 0);
                bContinue = FALSE;  
                break;
            }
            //
            // Get the next MP_RBD to process
            //
			CurrentRBD = Adapter->CurrentRBD ; 

            //pMpRbd = (PMP_RBD)GetListHeadEntry(&Adapter->RecvList);
			status = GetMpRbd ( Adapter , CurrentRBD, &pMpRbd );
			
			//
			// sanity check
			ASSERT ( (PUCHAR)pMpRbd->HwRbd == \
				Adapter->HwRbdBase + CurrentRBD * sizeof ( HW_RBD ) ) ;

			// Can this happen ?
			// Have to investigate further 
			if ( !(MP_TEST_FLAG(pMpRbd, fMP_RBD_RECV_READY)) ) {
				KdBreakPoint();
                bContinue = FALSE;  
                break;
            }
            
            //
            // Is this packet completed?
            //
            PacketStatus = NIC_RBD_STATUS(pMpRbd);
            if (!NIC_RBD_STATUS_COMPLETED(PacketStatus))
            {
                bContinue = FALSE;
                break;
            }

#if DBG_RCV_INTERRUPT
			KdPrint(("\n%d",pMpRbd->HwRecvBufferNumber));
			Adapter->LastRbdRcvdByNic = pMpRbd->HwRbd ;
#endif
            
			//
            // Remove the RECV_BUFFER from the the List
			// not necessarily the HEAD
            //
			RemoveEntryList((PLIST_ENTRY)pMpRbd);
			Adapter->pMpRbds [ CurrentRBD ] = NULL ;

			IncCurrentRBD ( Adapter ) ;
            Adapter->nReadyRecv--;
            ASSERT(Adapter->nReadyRecv >= 0);
            
            ASSERT(MP_TEST_FLAG(pMpRbd, fMP_RBD_RECV_READY));
            MP_CLEAR_FLAG(pMpRbd, fMP_RBD_RECV_READY);

            //
            // A good frame? drop it if not.
            //
            if ( NIC_RBD_STATUS_SUCCESS(PacketStatus))
            {
                DBGPRINT(MP_WARN, ("Receive failure = %x\n", PacketStatus));
                NICReturnRBD(Adapter, pMpRbd);
                continue;
            }
            //
            // HW specific - check if actual count field has been updated
            //
            if (!NIC_RBD_VALID_ACTUALCOUNT(pMpRbd))
            {
                DBGPRINT(MP_WARN, \
					("Receive completed by NIC, but BAD frame length reported = %x\n", \
					NIC_RBD_GET_PACKET_SIZE(pMpRbd)));
                NICReturnRBD(Adapter, pMpRbd);
                continue;
            }

            //
            // Do not receive any packets until a filter has been set
            //
            if (!Adapter->PacketFilter)
            {
				NICReturnRBD(Adapter, pMpRbd);
                continue;
            }

            //
            // Do not receive any packets until we are at D0
            //
            if (Adapter->CurrentPowerState != NdisDeviceStateD0)
            {
				NICReturnRBD(Adapter, pMpRbd);
                continue;
            }

            //
            // Get the packet size
            //
            PacketSize = NIC_RBD_GET_PACKET_SIZE(pMpRbd);

            NetBuffer = NET_BUFFER_LIST_FIRST_NB(pMpRbd->NetBufferList);
            //
            // During the call NdisAllocateNetBufferAndNetBufferList to allocate the NET_BUFFER_LIST, NDIS already
            // initializes DataOffset, CurrentMdl and CurrentMdlOffset, here the driver needs to update DataLength
            // in the NET_BUFFER to reflect the received frame size.
            // 
            NET_BUFFER_DATA_LENGTH(NetBuffer) = PacketSize;
            NdisAdjustMdlLength(pMpRbd->Mdl, PacketSize);
            NdisFlushBuffer(pMpRbd->Mdl, FALSE);

            //
            // Update the statistics
            //
            MP_RECEIVE_STATS(Adapter, NetBuffer);
			
            //
            // set the status on the packet, either resources or success
            //
            if (Adapter->nReadyRecv >= MIN_NUM_RECV_BUFFER)
            {	            
                //
                // Success case: NDIS will pend the NetBufferLists
                // 
                MP_SET_FLAG(pMpRbd, fMP_RBD_RECV_PEND);
                
				InsertTailList(&Adapter->RecvPendList, (PLIST_ENTRY)pMpRbd);
				
				MP_INC_RCV_REF(Adapter);
				
                if (NetBufferList == NULL)
                {
                    NetBufferList = pMpRbd->NetBufferList;
                }
                else
                {
                    NET_BUFFER_LIST_NEXT_NBL(PrevNetBufferList) = pMpRbd->NetBufferList;
                }
                NET_BUFFER_LIST_NEXT_NBL(pMpRbd->NetBufferList) = NULL;
                MP_CLEAR_FLAG(pMpRbd->NetBufferList, (NET_BUFFER_LIST_FLAGS(pMpRbd->NetBufferList) & NBL_FLAGS_MINIPORT_RESERVED));
                PrevNetBufferList = pMpRbd->NetBufferList;
            }
            else
            {				
                //
                // Resources case: Miniport will retain ownership of the NetBufferLists
                //
                MP_SET_FLAG(pMpRbd, fMP_RBD_RESOURCES);

                if (FreeNetBufferList == NULL)
                {
                    FreeNetBufferList = pMpRbd->NetBufferList;
                }
                else
                {
                    NET_BUFFER_LIST_NEXT_NBL(PrevFreeNetBufferList) = pMpRbd->NetBufferList;
                }
#pragma prefast(suppress: 8182, "pMpRbd->NetBufferList can never to NULL")                
                NET_BUFFER_LIST_NEXT_NBL(pMpRbd->NetBufferList) = NULL;                
                PrevFreeNetBufferList = pMpRbd->NetBufferList;
                MP_CLEAR_FLAG(pMpRbd->NetBufferList, (NET_BUFFER_LIST_FLAGS(pMpRbd->NetBufferList) & NBL_FLAGS_MINIPORT_RESERVED));
                NetBufferListFreeCount++;
                //
                // Reset the RECV_BUFFER shrink count - don't attempt to shrink RECV_BUFFER
                //
                Adapter->RecvBufferShrinkCount = 0;
                
                //
                // Remember to allocate a new RECV_BUFFER later
                //
				bAllocNewRecvBuffer = TRUE;
            }

            NetBufferListCount++;

        }

        //
        // if we didn't process any receives, just return from here
        //
        if (NetBufferListCount == 0) 
        {
            break;
        }
	
        //
        // Update the number of outstanding Recvs
        //
        Adapter->PoMgmt.OutstandingRecv += NetBufferListCount;
				
        NdisDprReleaseSpinLock(&Adapter->RcvLock);
        
        //
        // Indicate the NetBufferLists to NDIS
        //
        if (NetBufferList != NULL)
        {				
			NdisMIndicateReceiveNetBufferLists(
                    MP_GET_ADAPTER_HANDLE(Adapter),
                    NetBufferList,
                    NDIS_DEFAULT_PORT_NUMBER,
                    NetBufferListCount-NetBufferListFreeCount,
                    ReceiveFlags); 
		}
        
        if (FreeNetBufferList != NULL)
        {				
            NDIS_SET_RECEIVE_FLAG(ReceiveFlags, NDIS_RECEIVE_FLAGS_RESOURCES);
            
            NdisMIndicateReceiveNetBufferLists(
                    MP_GET_ADAPTER_HANDLE(Adapter),
                    FreeNetBufferList,          
                    NDIS_DEFAULT_PORT_NUMBER,
                    NetBufferListFreeCount,
                    ReceiveFlags); 
			
        }
        
        NdisDprAcquireSpinLock(&Adapter->RcvLock);

        //
        // NDIS won't take ownership for the packets with NDIS_STATUS_RESOURCES.
        // For other packets, NDIS always takes the ownership and gives them back 
        // by calling MPReturnPackets
        //
           
        for (; FreeNetBufferList != NULL; FreeNetBufferList = NET_BUFFER_LIST_NEXT_NBL(FreeNetBufferList))
        {

            //
            // Get the MP_RBD saved in this packet, in NICAllocRBD
            //
            pMpRbd = MP_GET_NET_BUFFER_LIST_RECV_BUFFER(FreeNetBufferList);
            
            ASSERT(MP_TEST_FLAG(pMpRbd, fMP_RBD_RESOURCES));
            MP_CLEAR_FLAG(pMpRbd, fMP_RBD_RESOURCES);

            //
            // Decrement the number of outstanding Recvs
            //            				
            Adapter->PoMgmt.OutstandingRecv--;
			
            NICReturnRBD(Adapter, pMpRbd);
        }

        //
        // If we have set power pending, then complete it
        //
        if (((Adapter->PendingRequest)
                && ((Adapter->PendingRequest->RequestType == NdisRequestSetInformation)
                && (Adapter->PendingRequest->DATA.SET_INFORMATION.Oid == OID_PNP_SET_POWER)))
                && (Adapter->PoMgmt.OutstandingRecv == 0))
        {
            MpSetPowerLowComplete(Adapter);
        }
        
    }
	
    if (AllocateIoWorkItem)
    {
        NDIS_HANDLE NdisIoWorkItemHandle = NULL;
        //
        // demonstrate allocating an NDIS IO workitem
        //

        NdisIoWorkItemHandle = NdisAllocateIoWorkItem(Adapter->AdapterHandle);
        ASSERT(NdisIoWorkItemHandle != NULL);
            
        if (NdisIoWorkItemHandle)
        {
            NdisQueueIoWorkItem(NdisIoWorkItemHandle,
                                MPQueuedWorkItem,
                                NdisIoWorkItemHandle);
            
        }
        AllocateIoWorkItem = FALSE;
    }
    
    //
    // If we ran low on RECV_BUFFER's, we need to allocate a new RECV_BUFFER
    //
    if (bAllocNewRecvBuffer)
    {
        //
        // Allocate one more RECV_BUFFER only if no pending new RECV_BUFFER allocation AND
        // it doesn't exceed the max RECV_BUFFER limit
        //
        if (!Adapter->bAllocNewRecvBuffer && Adapter->CurrNumRecvBuffer < Adapter->MaxNumRecvBuffer)
        {
            PMP_RBD TempMpRbd;
            NDIS_STATUS TempStatus;

            TempMpRbd = NdisAllocateFromNPagedLookasideList(&Adapter->RecvLookaside);
            if (TempMpRbd)
            {
                MP_INC_REF(Adapter);
                Adapter->bAllocNewRecvBuffer = TRUE;

				NdisZeroMemory ( TempMpRbd, sizeof ( MP_RBD ) ) ;

                MP_SET_FLAG(TempMpRbd, fMP_RBD_ALLOC_PEND); 

                //
                // Allocate the shared memory for this RECV_BUFFER.
                //
                TempStatus = NdisMAllocateSharedMemoryAsyncEx(
                                 Adapter->NdisMiniportDmaHandle,
                                 Adapter->HwRecvBufferSize,
                                 TRUE ,
                                 TempMpRbd);

                //
                // The return value is not NDIS_STATUS_PENDING, and Allocation failed
                //
                if (TempStatus != NDIS_STATUS_PENDING)
                {
                    MP_CLEAR_FLAGS(TempMpRbd);
                    NdisFreeToNPagedLookasideList(&Adapter->RecvLookaside, TempMpRbd);

                    Adapter->bAllocNewRecvBuffer = FALSE;
                    MP_DEC_REF(Adapter);
                }
            }
        }
    }

    //
    // get rid of the extra receive ref count we added at the beginning of this
    // function and check to see if we need to complete the pause.
    // Note that we don't have to worry about a blocked Halt here because
    // we are handling an interrupt DPC which means interrupt deregistertion is not
    // completed yet so even if our halt handler is running, NDIS will block the
    // interrupt deregisteration till we return back from this DPC.
    //
    MP_DEC_RCV_REF(Adapter);

    Count =  MP_GET_RCV_REF(Adapter);
    if ((Count == 0) && (Adapter->AdapterState == NicPausing))
    {
        //
        // If all the NetBufferLists are returned and miniport is pausing,
        // complete the pause
        // 
        
        Adapter->AdapterState = NicPaused;
        NdisDprReleaseSpinLock(&Adapter->RcvLock);
        NdisMPauseComplete(Adapter->AdapterHandle);
        NdisDprAcquireSpinLock(&Adapter->RcvLock);
    }
        
    ASSERT(Adapter->nReadyRecv >= NIC_MIN_RECV_BUFFERS);

    DBGPRINT(MP_TRACE, ("<--- MpHandleRecvInterrupt\n"));
}

VOID
MPQueuedWorkItem(
    IN PVOID                        WorkItemContext,
    IN NDIS_HANDLE                  NdisIoWorkItemHandle
    )
{
    UNREFERENCED_PARAMETER(WorkItemContext);
    DBGPRINT(MP_INFO,("MPQueuedWorkItem, WorkItemContext = %p\n", WorkItemContext));
    
    NdisFreeIoWorkItem((NDIS_HANDLE)NdisIoWorkItemHandle);
    
}



VOID 
NICReturnRBD(
    IN  PMP_ADAPTER  Adapter,
    IN  PMP_RBD      pMpRbd
    )
/*++
Routine Description:

    Recycle a RECV_BUFFER and put it back onto the receive list 
    Assumption: Rcv spinlock has been acquired 

Arguments:

    Adapter     Pointer to our adapter
    pMpRbd      Pointer to the RECV_BUFFER 

Return Value:

    None
    
    NOTE: During return, we should check if we need to allocate new net buffer list
          for the RECV_BUFFER.
--*/
{

	InsertMpRbdInFreeHwRbdsArray ( Adapter, pMpRbd  ) ;  

    InsertTailList(&Adapter->RecvList, (PLIST_ENTRY)pMpRbd);
    
	MP_SET_FLAG(pMpRbd, fMP_RBD_RECV_READY);

#if DBG_RCV_INTERRUPT
			KdPrint(("\n+%d",pMpRbd->HwRecvBufferNumber));
			//sprintf(dbgrcvbuf[dbgrcvindex++], "\n%d",pMpRbd->HwRecvBufferNumber ) ;
#endif

	// Initialize/Zero-out RECV_BUFFER and set OWN bit to push
	// ownership to NIC HW
	ZeroRBDandDisOwnToNIC ( 
			pMpRbd->HwRbd,
			pMpRbd
			) ;

    Adapter->nReadyRecv++;

    ASSERT(Adapter->nReadyRecv <= Adapter->CurrNumRecvBuffer);
}

NDIS_STATUS 
NICStartRecv(
    IN  PMP_ADAPTER  Adapter
    )
/*++
Routine Description:

    Start the receive unit if it's not in a ready state                    
    Assumption: Rcv spinlock has been acquired 

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_HARD_ERRROS
    
--*/
{
    PMP_RBD         pMpRbd;
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS ;
	ULONG_PTR		ioaddr = Adapter->PortOffset ;

    DBGPRINT(MP_TRACE, ("---> NICStartRecv\n"));

    //
    // If the receiver is ready, then don't try to restart.
    //
    if (NIC_IS_RECV_READY(Adapter))
    {
        DBGPRINT(MP_LOUD, ("Receive unit already active\n"));
        return NDIS_STATUS_SUCCESS;
    }

    DBGPRINT(MP_LOUD, ("Re-start receive unit...\n"));
    
    ASSERT(!IsListEmpty(&Adapter->RecvList));
    
    //
    // Get the MP_RBD head
    //
    pMpRbd = (PMP_RBD)GetListHeadEntry(&Adapter->RecvList);

    //
    // If more packets are received, clean up RECV_BUFFER chain again
    //
    if ( NIC_RBD_STATUS_COMPLETED ( NIC_RBD_STATUS ( pMpRbd ) ) )
    {
        MpHandleRecvInterrupt(Adapter);
        ASSERT(!IsListEmpty(&Adapter->RecvList));

        //
        // Get the new MP_RBD head
        //
        pMpRbd = (PMP_RBD)GetListHeadEntry(&Adapter->RecvList);
    }

    if (Adapter->CurrentPowerState > NdisDeviceStateD0)
    {
        Status = NDIS_STATUS_HARD_ERRORS;
        MP_EXIT;
    }
    
    //
    // START the receive unit
    //
    RealtekHwCommand(Adapter, SCB_RUC_START, FALSE);
    
    exit:

    DBGPRINT_S(Status, ("<--- NICStartRecv, Status=%x\n", Status));
    return Status;
}

VOID 
MpFreeQueuedSendNetBufferLists(
    IN  PMP_ADAPTER  Adapter
    )
/*++
Routine Description:

    Free and complete the pended sends on SendWaitQueue
    Assumption: spinlock has been acquired 
    
Arguments:

    Adapter     Pointer to our adapter

Return Value:

     None
NOTE: Run at DPC     

--*/
{
    PQUEUE_ENTRY        pEntry;
    PNET_BUFFER_LIST    NetBufferList;
    PNET_BUFFER_LIST    NetBufferListToComplete = NULL;
    PNET_BUFFER_LIST    LastNetBufferList = NULL;
    NDIS_STATUS         Status = MP_GET_STATUS_FROM_FLAGS(Adapter);
    PNET_BUFFER         NetBuffer;

    DBGPRINT(MP_TRACE, ("--> MpFreeQueuedSendNetBufferLists\n"));

    while (!IsQueueEmpty(&Adapter->SendWaitQueue))
    {
        pEntry = RemoveHeadQueue(&Adapter->SendWaitQueue); 
        ASSERT(pEntry);
        
        Adapter->nWaitSend--;

        NetBufferList = MP_GET_NET_BUFFER_LIST_FROM_QUEUE_LINK(pEntry);

        NET_BUFFER_LIST_STATUS(NetBufferList) = Status;
        //
        // The sendLock is held
        // 
        NetBuffer = MP_GET_NET_BUFFER_LIST_NEXT_SEND(NetBufferList);
        
        for (; NetBuffer != NULL; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList)--;
        }
        //
        // If Ref count goes to 0, then complete it.
        // Otherwise, Send interrupt DPC would complete it later
        // 
        if (MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList) == 0)
        {
            if (NetBufferListToComplete == NULL)
            {
                NetBufferListToComplete = NetBufferList;
            }
            else
            {
                NET_BUFFER_LIST_NEXT_NBL(LastNetBufferList) = NetBufferList;
            }
            NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
            LastNetBufferList = NetBufferList;

        }
    }

    if (NetBufferListToComplete != NULL)
    {
        MP_RELEASE_SPIN_LOCK(&Adapter->SendLock, TRUE);
        NdisMSendNetBufferListsComplete(
               MP_GET_ADAPTER_HANDLE(Adapter),
               NetBufferListToComplete,
               NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);   

        MP_ACQUIRE_SPIN_LOCK(&Adapter->SendLock, TRUE);
    }
    
    DBGPRINT(MP_TRACE, ("<-- MpFreeQueuedSendNetBufferLists\n"));

}

void 
MpFreeBusySendNetBufferLists(
    IN  PMP_ADAPTER  Adapter
    )
/*++
Routine Description:

    Free and complete the stopped active sends
    Assumption: Send spinlock has been acquired 
    
Arguments:

    Adapter     Pointer to our adapter

Return Value:

     None

--*/
{
    PMP_TCB  pMpTcb;
    PNET_BUFFER_LIST   NetBufferList;
    PNET_BUFFER_LIST   CompleteNetBufferLists = NULL;
    PNET_BUFFER_LIST   LastNetBufferList = NULL;

    DBGPRINT(MP_TRACE, ("--> MpFreeBusySendNetBufferLists\n"));

    //
    // Any NET_BUFFER being sent? Check the first TBD on the send list
    //
    while (Adapter->nBusySend > 0)
    {
        ASSERT(Adapter->CurrSendHead != Adapter->CurrSendTail);

		pMpTcb = Adapter->CurrSendHead;

		//
		// Is this TBD completed?
		//
		if (1)			////new additions for realtek driver
		//if ((pMpTcb->HwTbd->TxCbHeader.CbCommand & CB_CMD_MASK) != CB_MULTICAST)
        {
            //
            // To change this to complete a list of NET_BUFFER_LISTs
            //
            NetBufferList = MP_FREE_SEND_NET_BUFFER(Adapter, pMpTcb);
            //
            // If one NET_BUFFER_LIST got complete
            //
            if (NetBufferList != NULL)
            {
                NET_BUFFER_LIST_STATUS(NetBufferList) = NDIS_STATUS_REQUEST_ABORTED;
                if (CompleteNetBufferLists == NULL)
                {
                    CompleteNetBufferLists = NetBufferList;
                }
                else
                {
                    NET_BUFFER_LIST_NEXT_NBL(LastNetBufferList) = NetBufferList;
                }
                NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
                LastNetBufferList = NetBufferList;
            }
            
        }
        else
        {
            break;
        }
        
    }

    //
    // Complete the NET_BUFFER_LISTs
    //
    if (CompleteNetBufferLists != NULL)
    {
        NdisReleaseSpinLock(&Adapter->SendLock);
        NdisMSendNetBufferListsComplete(
           MP_GET_ADAPTER_HANDLE(Adapter),
           CompleteNetBufferLists,
           0);       
        NdisAcquireSpinLock(&Adapter->SendLock);
    }

    DBGPRINT(MP_TRACE, ("<-- MpFreeBusySendNetBufferLists\n"));
}

// asumption HW receiver is already stopped/disabled
VOID 
NICResetRecv(
    IN  PMP_ADAPTER   Adapter
    )
/*++
Routine Description:

    Reset the receive list                    
    Assumption: Rcv spinlock has been acquired 
    
Arguments:

    Adapter     Pointer to our adapter

Return Value:

     None

--*/
{
    PMP_RBD   oldhead, pMpRbd ;      
    LONG      RecvBufferCount;

    DBGPRINT(MP_TRACE, ("--> NICResetRecv\n"));

    ASSERT(!IsListEmpty(&Adapter->RecvList));

	// reset RECEIVE state machine to START
	Adapter->CurrentRBD = 0 ;
	Adapter->EarlyFreeRbdHint = 0 ;
	ZeroOutMpRbdsArray ( Adapter ) ;

    //
    // FREE all MP_RBDs and reassign them to ready Q
    //
	for (	RecvBufferCount = 0; 
			RecvBufferCount < Adapter->nReadyRecv ;
			RecvBufferCount++
	)
    {
        pMpRbd = (PMP_RBD)RemoveHeadList(&Adapter->RecvList);
		
		if ( RecvBufferCount == 0 )	{ // if first RBD only
			oldhead = pMpRbd ;			// take a copy to test for wrap around later
		}
		else
		{
			ASSERTMSG ( "MP_RBD wrapped around. BAD Adapter->nReadyRecv value may be\n" , \
						(oldhead != pMpRbd ) \
			) ;
		}
        
		// make sure this RBD is really from the ready Q
		ASSERT(MP_TEST_FLAG(pMpRbd, fMP_RBD_RECV_READY));

		// return RBD to ready Q
        MP_CLEAR_FLAG(pMpRbd, fMP_RBD_RECV_READY);
		Adapter->nReadyRecv--;
		NICReturnRBD(Adapter, pMpRbd); // this does a nReadyRecv++
   }

    DBGPRINT(MP_TRACE, ("<-- NICResetRecv\n"));
}

//asumption : NO lock is held
VOID 
MpHandleLinkChangeInterrupt(
				   PMP_ADAPTER         Adapter
				   )
/*++

Routine Description:
    
    Timer function for postponed link negotiation
    
Arguments:
	ADapter

Return Value:

    None
    
--*/
{
    ULONG               MediaState;
   
    DBGPRINT(MP_TRACE, ("--> MpHandleLinkChangeInterrupt\n"));
    //
    // NDIS 6.0 miniports are required to report their link speed, link state and
    // duplex state as soon as they figure it out. NDIS does not make any assumption
    // that they are connected, etc.
    //

    MediaState = NICGetMediaState(Adapter);
    NdisDprAcquireSpinLock(&Adapter->Lock);
    
    Adapter->MediaState = MediaState;
    
	MPIndicateLinkState(Adapter);

	if ( MediaState == MediaConnectStateConnected )
		MpHandleLinkUp ( Adapter ) ;
	
	else
		MpHandleLinkDown ( Adapter ) ;
	

	DBGPRINT(MP_TRACE, ("<-- MpHandleLinkChangeInterrupt\n"));

}

// adapter->lock acquired
VOID
MpHandleLinkUp ( PMP_ADAPTER Adapter )
{
    NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;
    NDIS_STATUS         IndicateStatus;
    PQUEUE_ENTRY        pEntry = NULL;
    PNDIS_OID_REQUEST   PendingRequest;
    NDIS_REQUEST_TYPE   RequestType;
    NDIS_OID            Oid;
    ULONG               PacketFilter;
    ULONG               LinkSpeed;
    ULONG               MediaState;
    LARGE_INTEGER       liDueTime;
    BOOLEAN             isTimerAlreadyInQueue = FALSE;
    

	DBGPRINT(MP_TRACE, ("--> MpHandleLinkUp\n"));

	MediaState = Adapter->MediaState ;

    //
    // Any pending request?                                                        
    //
    if (Adapter->PendingRequest)
    {
        PendingRequest = Adapter->PendingRequest;
        Adapter->PendingRequest = NULL;

        RequestType = PendingRequest->RequestType;

        NdisDprReleaseSpinLock(&Adapter->Lock);

        switch(RequestType)
        {
            case NdisRequestQueryInformation:
            case NdisRequestQueryStatistics:
                Oid = PendingRequest->DATA.QUERY_INFORMATION.Oid;
                Status = NDIS_STATUS_SUCCESS;          
                switch (Oid)
                {
                    case OID_GEN_LINK_SPEED:
                        LinkSpeed = Adapter->usLinkSpeed * 10000;
                        NdisMoveMemory(PendingRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                                        &LinkSpeed,
                                        sizeof(ULONG));

                        PendingRequest->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);

                        break;

                    case OID_GEN_MEDIA_CONNECT_STATUS:
                    default:
                        ASSERT(PendingRequest->DATA.QUERY_INFORMATION.Oid == OID_GEN_MEDIA_CONNECT_STATUS);
                        NdisMoveMemory(PendingRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                                        &MediaState,
                                        sizeof(ULONG));
                        NdisDprAcquireSpinLock(&Adapter->Lock);
                        if (Adapter->MediaState != MediaState)
                        {
                            Adapter->MediaState = MediaState;
      
                        }
                        NdisDprReleaseSpinLock(&Adapter->Lock);
                        
                    PendingRequest->DATA.QUERY_INFORMATION.BytesWritten = sizeof(NDIS_MEDIA_STATE);
                    break;
                    
                }
                PendingRequest->DATA.QUERY_INFORMATION.BytesNeeded = 0;
                break;

            case NdisRequestSetInformation:
                //
                // It has to be set packet filter
                // 
                Oid = PendingRequest->DATA.QUERY_INFORMATION.Oid;
                if (Oid == OID_GEN_CURRENT_PACKET_FILTER)
                {
                    NdisMoveMemory(&PacketFilter,
                                     PendingRequest->DATA.SET_INFORMATION.InformationBuffer,
                                     sizeof(ULONG));

                    NdisDprAcquireSpinLock(&Adapter->Lock);
 
                    Status = NICSetPacketFilter(
                                 Adapter,
                                 PacketFilter);

                    NdisDprReleaseSpinLock(&Adapter->Lock);

                    if (Status == NDIS_STATUS_SUCCESS)
                    {
                        PendingRequest->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
                        Adapter->PacketFilter = PacketFilter;
                
                    }
                    PendingRequest->DATA.QUERY_INFORMATION.BytesNeeded = 0;
                }
                break;
            
            case NdisRequestMethod:
                Status = MpMethodRequest (Adapter, PendingRequest);
                break;

            default:
                ASSERT(FALSE);
                break;
        } //end of outside switch
        if (Status != NDIS_STATUS_PENDING)
        {
            NdisMOidRequestComplete(Adapter->AdapterHandle, 
                                    PendingRequest,
                                    Status);
        }

        NdisDprAcquireSpinLock(&Adapter->Lock);
    }

    //
    // Adapter->Lock is held
    //
    // Any pending reset?
    //
    if (Adapter->bResetPending)
    {
        //
        // The link detection may have held some requests and caused reset. 
        // Don't need to complete the reset, since the status is not SUCCESS.
        //
        Adapter->bResetPending = FALSE;
        MP_CLEAR_FLAG(Adapter, fMP_ADAPTER_RESET_IN_PROGRESS);

        NdisDprReleaseSpinLock(&Adapter->Lock);

        NdisMResetComplete(
            Adapter->AdapterHandle,     
            NDIS_STATUS_ADAPTER_NOT_READY,
            FALSE);
    }
    else
    {
        NdisDprReleaseSpinLock(&Adapter->Lock);
    }

    NdisDprAcquireSpinLock(&Adapter->RcvLock);

    //
    // Start the NIC receive unit                                                     
    //
    Status = NICStartRecv(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        MP_SET_HARDWARE_ERROR(Adapter);
		DBGPRINT(MP_WARN, ("NICStartRecv FAILED\n"));
    }
    
    NdisDprReleaseSpinLock(&Adapter->RcvLock);
    NdisDprAcquireSpinLock(&Adapter->SendLock);

    //
    // Send NET_BUFFER_LISTs which have been queued while link detection was going on. 
    //
    if (MP_IS_READY(Adapter))
    {
        while (!IsQueueEmpty(&Adapter->SendWaitQueue) &&
            (MP_TCB_RESOURCES_AVAIABLE(Adapter) &&
            Adapter->SendingNetBufferList == NULL))
        {
            PNET_BUFFER_LIST    NetBufferList;
                       
            pEntry = GetHeadQueue(&Adapter->SendWaitQueue); 
            ASSERT(pEntry);
            
            NetBufferList = MP_GET_NET_BUFFER_LIST_FROM_QUEUE_LINK(pEntry);

            DBGPRINT(MP_INFO, ("MpHandleLinkChangeInterrupt - send a queued NetBufferList\n"));

            Adapter->SendingNetBufferList = NetBufferList;
            
            Status = MiniportSendNetBufferList(Adapter, NetBufferList, TRUE);

            if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_PENDING)
            {
                break;
            }
        }   
    }

	NdisDprReleaseSpinLock(&Adapter->SendLock);

	DBGPRINT(MP_TRACE, ("<-- MpHandleLinkUp\n"));
 }

VOID
MpHandleLinkDown ( PMP_ADAPTER Adapter )
{
	DBGPRINT(MP_TRACE, ("--> MpHandleLinkDown\n"));

	NdisDprReleaseSpinLock(&Adapter->Lock);

	DBGPRINT(MP_TRACE, ("<-- MpHandleLinkDown\n"));
}

VOID
MpProcessSGList(
    IN  PDEVICE_OBJECT          pDO,
    IN  PVOID                   pIrp,
    IN  PSCATTER_GATHER_LIST    pSGList,
    IN  PVOID                   Context
    )
/*++
Routine Description:

    Process  SG list for an NDIS packet or a NetBuffer by submitting the physical addresses
    in SG list to NIC's DMA engine and issuing command n hardware.
    
Arguments:

    pDO:  Ignore this parameter
    
    pIrp: Ignore this parameter
    
    pSGList: A pointer to Scatter Gather list built for the NDIS packet or NetBuffer passed
             to NdisMAllocNetBufferList. This is not necessarily
             the same ScatterGatherListBuffer passed to NdisMAllocNetBufferSGList
             
    Context: The context passed to NdisMAllocNetBufferList. Here is 
             a pointer to MP_TCB
             
Return Value:

     None

    NOTE: called at DISPATCH level
--*/
{
    PMP_TCB             pMpTcb = (PMP_TCB)Context;
    PMP_ADAPTER         Adapter = pMpTcb->Adapter;
    PMP_FRAG_LIST       pFragList = (PMP_FRAG_LIST)pSGList;
    NDIS_STATUS         Status;
    MP_FRAG_LIST        FragList;
    BOOLEAN             fSendComplete = FALSE;
    ULONG               BytesCopied;
    PNET_BUFFER_LIST    NetBufferList;
    
    DBGPRINT(MP_TRACE, ("--> MpProcessSGList\n"));

    MP_ACQUIRE_SPIN_LOCK(&Adapter->SendLock, TRUE);

    //
    // Save SG list that we got from NDIS. This is not necessarily the
    // same SG list buffer we passed to NdisMAllocNetBufferSGList
    //
    pMpTcb->pSGList = pSGList;
        
    if (!MP_TEST_FLAG(pMpTcb, fMP_TCB_IN_USE))
    {
        
        //
        // Before this callback function is called, reset happened and aborted
        // all the sends.
        // Call ndis to free resouces allocated for a SG list
        //
        MP_RELEASE_SPIN_LOCK(&Adapter->SendLock, TRUE);

		if ( pMpTcb->pSGList && pMpTcb->NetBuffer)	{
			
			NdisMFreeNetBufferSGList(
				Adapter->NdisMiniportDmaHandle,
				pMpTcb->pSGList,
				pMpTcb->NetBuffer);
		}
        
    }
    else
    {
        if (pSGList->NumberOfElements > NIC_MAX_PHYS_BUF_COUNT )
        {
            //
            // the driver needs to do the local copy
            // 
            
            BytesCopied = MpCopyNetBuffer(pMpTcb->NetBuffer, pMpTcb->MpTxBuf);
                        
            //
            // MpCopyNetBuffer may return 0 if system resources are low or exhausted
            //
            if (BytesCopied == 0)
            {
                           
                DBGPRINT(MP_ERROR, ("Copy NetBuffer NDIS_STATUS_RESOURCES, NetBuffer= "PTR_FORMAT"\n", pMpTcb->NetBuffer));
                NetBufferList = pMpTcb->NetBufferList;
                NET_BUFFER_LIST_STATUS(NetBufferList) = NDIS_STATUS_RESOURCES;
                
                MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList)--;
                
                if (MP_GET_NET_BUFFER_LIST_REF_COUNT(NetBufferList) == 0)
                {
                    fSendComplete = TRUE;
                }
             
                MP_RELEASE_SPIN_LOCK(&Adapter->SendLock, TRUE);
                NdisMFreeNetBufferSGList(
                    Adapter->NdisMiniportDmaHandle,
                    pMpTcb->pSGList,
                    pMpTcb->NetBuffer);

                if (fSendComplete)
                {

                    NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
        
                    NdisMSendNetBufferListsComplete(
                        MP_GET_ADAPTER_HANDLE(Adapter),
                        NetBufferList,
                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);   
                }
             
             }
             else
             {
                MP_SET_FLAG(pMpTcb, fMP_TCB_USE_LOCAL_BUF);

                //
                // Set up the frag list, only one fragment after it's coalesced
                //
                pFragList = &FragList;
                pFragList->NumberOfElements = 1;
                pFragList->Elements[0].Address = pMpTcb->MpTxBuf->BufferPa;
                pFragList->Elements[0].Length = (BytesCopied >= NIC_MIN_PACKET_SIZE) ?
                                                BytesCopied : NIC_MIN_PACKET_SIZE;
                
                Status = NICSendNetBuffer(Adapter, pMpTcb, pFragList);
                MP_RELEASE_SPIN_LOCK(&Adapter->SendLock, TRUE);
             }
        }
        else
        {
            Status = NICSendNetBuffer(Adapter, pMpTcb, pFragList);
            MP_RELEASE_SPIN_LOCK(&Adapter->SendLock, TRUE);
        }
    }

    DBGPRINT(MP_TRACE, ("<-- MpProcessSGList\n"));
}

VOID
MPIndicateLinkState(
    IN  PMP_ADAPTER     Adapter
    )    
/*++
Routine Description:
    This routine sends a NDIS_STATUS_LINK_STATE status up to NDIS
    
Arguments:
    
    Adapter         Pointer to our adapter
             
Return Value:

     None

    NOTE: called at DISPATCH level
--*/

{
    
    NDIS_LINK_STATE                LinkState;
    NDIS_STATUS_INDICATION         StatusIndication;
	ULONG_PTR ioaddr = (ULONG_PTR)Adapter->PortOffset ;

    NdisZeroMemory(&LinkState, sizeof(NDIS_LINK_STATE));
    
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    
    DBGPRINT(MP_WARN, ("Media state changed to %s\n",
              ((Adapter->MediaState == MediaConnectStateConnected)? 
              "Connected": "Disconnected")));

	
    if (Adapter->MediaState == MediaConnectStateConnected)
    {
        
		if(RTL_R8(PHYstatus) & _1000Mbps) {
			Adapter->usLinkSpeed = 1000 ;
			DBGPRINT( MP_INFO, \
				("Link Speed:1000Mbps\n"));
		}

		else if(RTL_R8(PHYstatus) & _100Mbps) {
			Adapter->usLinkSpeed = 100 ;
			DBGPRINT( MP_INFO, \
				("Link Speed:100Mbps\n"));
		}
		else if(RTL_R8(PHYstatus) & _10Mbps) {
			Adapter->usLinkSpeed = 1000 ;
			DBGPRINT( MP_INFO,\
				("Link Speed:10Mbps\n"));
		}
		
		Adapter->usDuplexMode = ( RTL_R8( PHYstatus ) & FullDup ) ? 
								2 : 1 ;

		// dump some PHY status
		KdPrint(( "\nGBSR is 0x%0X\n", \
			RTL8169_READ_GMII_REG( ioaddr, PHY_GBSR_REG ) \
			)) ;
		KdPrint(( "GBESR is 0x%0X\n", \
			RTL8169_READ_GMII_REG( ioaddr, PHY_GBESR_REG ) \
			)) ;
        
		MP_CLEAR_FLAG(Adapter, fMP_ADAPTER_NO_CABLE);
        
		if (Adapter->usDuplexMode == 1 )
		{
            Adapter->MediaDuplexState = MediaDuplexStateHalf;
			DBGPRINT( MP_INFO, \
				("Duplex mode : Half-Duplex \n"));        
        }
        else if (Adapter->usDuplexMode == 2)
        {
            Adapter->MediaDuplexState = MediaDuplexStateFull;
			DBGPRINT( MP_INFO, \
				("Duplex mode : Full-Duplex \n"));        
        }
        else
        {
            Adapter->MediaDuplexState = MediaDuplexStateUnknown;
			DBGPRINT( MP_INFO, \
				("Duplex mode : UNKNOWN \n"));        
        }
        //
        // NDIS 6.0 miniports report xmit and recv link speeds in bps
        //
        Adapter->LinkSpeed = Adapter->usLinkSpeed * 1000000;
    }
    else
    {
        MP_SET_FLAG(Adapter, fMP_ADAPTER_NO_CABLE);
        Adapter->MediaState = MediaConnectStateDisconnected;
        Adapter->MediaDuplexState = MediaDuplexStateUnknown;
        Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }

    LinkState.MediaConnectState = Adapter->MediaState;
    LinkState.MediaDuplexState = Adapter->MediaDuplexState;
    LinkState.XmitLinkSpeed = LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
   
    NdisDprReleaseSpinLock(&Adapter->Lock);
    MP_INIT_NDIS_STATUS_INDICATION(&StatusIndication,
                                   Adapter->AdapterHandle,
                                   NDIS_STATUS_LINK_STATE,
                                   (PVOID)&LinkState,
                                   sizeof(LinkState));
                                   
    NdisMIndicateStatusEx(Adapter->AdapterHandle, &StatusIndication);
    NdisDprAcquireSpinLock(&Adapter->Lock);
    
    return;
}

//asumption : NO lock is held
VOID
MpHandleLinkInterrupt ( PMP_ADAPTER Adapter, 
						UINT16 IntStatus 
						)
{

	ULONG_PTR	ioaddr = Adapter->PortOffset ;

    DBGPRINT(MP_TRACE, ("--> MpHandleLinkInterrupt\n"));

	// not a LINK status change interrupt ?
	if ( !(LinkChg & IntStatus ) )
		return ;

	MpHandleLinkChangeInterrupt ( Adapter );

	DBGPRINT(MP_TRACE, ("<-- MpHandleLinkInterrupt\n"));
}



