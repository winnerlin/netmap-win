/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:
    mp_init.c

Abstract:
    This module contains miniport initialization related routines

Revision History:

Notes:

--*/

#include "precomp.h"
#include "intsafe.h"
#include <ntintsafe.h>

#if DBG
#define _FILENUMBER     'TINI'
#endif

typedef struct _MP_REG_ENTRY
{
    NDIS_STRING RegName;                // variable name text
    BOOLEAN     bRequired;              // 1 -> required, 0 -> optional
    UINT        FieldOffset;            // offset to MP_ADAPTER field
    UINT        FieldSize;              // size (in bytes) of the field
    UINT        Default;                // default value to use
    UINT        Min;                    // minimum value allowed
    UINT        Max;                    // maximum value allowed
} MP_REG_ENTRY, *PMP_REG_ENTRY;

MP_REG_ENTRY NICRegTable[] = {
// reg value name                           Offset in MP_ADAPTER            Field size                  Default Value           Min             Max
#if DBG                                                                                                                          
    {NDIS_STRING_CONST("Debug"),            0, MP_OFFSET(Debug),            MP_SIZE(Debug),             MP_WARN,                0,              0xffffffff},
#endif
    {NDIS_STRING_CONST("*ReceiveBuffers"),  0, MP_OFFSET(NumRecvBuffer),           MP_SIZE(NumRecvBuffer),            NIC_MAX_RECV_BUFFERS/4,         NIC_MIN_RECV_BUFFERS,   NIC_MAX_RECV_BUFFERS},
    {NDIS_STRING_CONST("*TransmitBuffers"), 0, MP_OFFSET(NumTcb),           MP_SIZE(NumTcb),            NIC_DEF_TBDS,           1,              NIC_MAX_TBDS},
    {NDIS_STRING_CONST("PhyAddress"),       0, MP_OFFSET(PhyAddress),       MP_SIZE(PhyAddress),        0xFF,                   0,              0xFF},
    {NDIS_STRING_CONST("Connector"),        0, MP_OFFSET(Connector),        MP_SIZE(Connector),         0,                      0,              0x2},
    {NDIS_STRING_CONST("TxDmaCount"),       0, MP_OFFSET(AiTxDmaCount),     MP_SIZE(AiTxDmaCount),      0,                      0,              63},
    {NDIS_STRING_CONST("RxDmaCount"),       0, MP_OFFSET(AiRxDmaCount),     MP_SIZE(AiRxDmaCount),      0,                      0,              63},
    {NDIS_STRING_CONST("Threshold"),        0, MP_OFFSET(AiThreshold),      MP_SIZE(AiThreshold),       200,                    0,              200},
    {NDIS_STRING_CONST("MWIEnable"),        0, MP_OFFSET(MWIEnable),        MP_SIZE(MWIEnable),         1,                      0,              1},
    {NDIS_STRING_CONST("Congest"),          0, MP_OFFSET(Congest),          MP_SIZE(Congest),           0,                      0,              0x1},
    {NDIS_STRING_CONST("*SpeedDuplex"),     0, MP_OFFSET(SpeedDuplex),      MP_SIZE(SpeedDuplex),       0,                      0,              4}
};

#define NIC_NUM_REG_PARAMS (sizeof (NICRegTable) / sizeof(MP_REG_ENTRY))

NDIS_STATUS 
MpFindAdapter(
    IN  PMP_ADAPTER             Adapter,
    IN  PNDIS_RESOURCE_LIST     resList
    )
/*++
Routine Description:

    Find the adapter and get all the assigned resources

Arguments:

    Adapter     Pointer to our adapter
    resList     Pointer to our resources

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_ADAPTER_NOT_FOUND (event is logged as well)    

--*/    
{

    
    NDIS_STATUS         Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
    ULONG               ErrorCode = 0;
    ULONG               ErrorValue = 0;

    ULONG               ulResult;
    
	typedef __declspec(align(MEMORY_ALLOCATION_ALIGNMENT)) CommonConfigCharBuf;        
    
	CommonConfigCharBuf buffer[NIC_PCI_RLTK_HDR_LENGTH ];
    
	PPCI_COMMON_CONFIG  pPciConfig = (PPCI_COMMON_CONFIG) buffer;
    
	USHORT              usPciCommand;
       
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDesc;
    ULONG               index;
    BOOLEAN             bResPort = FALSE, bResInterrupt = FALSE, bResMemory = FALSE;

    DBGPRINT(MP_TRACE, ("---> MpFindAdapter\n"));

    do
    {
        //
        // Make sure the adpater is present
        //

        ulResult = NdisMGetBusData(
                       Adapter->AdapterHandle,
                       PCI_WHICHSPACE_CONFIG,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID),
                       buffer,
                       NIC_PCI_RLTK_HDR_LENGTH );
                       

        if (ulResult != NIC_PCI_RLTK_HDR_LENGTH )
        {
            DBGPRINT(MP_ERROR, 
                ("NdisMGetBusData (PCI_COMMON_CONFIG) ulResult=%d\n", ulResult));

            ErrorCode = NDIS_ERROR_CODE_ADAPTER_NOT_FOUND;
            ErrorValue = ERRLOG_READ_PCI_SLOT_FAILED;
                   
            break;
        }

        //     
        // Right type of adapter?
        //
        if (  pPciConfig->VendorID != NIC_PCI_VENDOR_ID || 
            
			!( pPciConfig->DeviceID == NIC_PCI_DEVICE_ID || 
			  pPciConfig->DeviceID == NIC_PCI_DEVICE_ID_2 || 
		 	  pPciConfig->DeviceID == NIC_PCI_DEVICE_ID_3 || 
			  pPciConfig->DeviceID == NIC_PCI_DEVICE_ID_4 || 
			  pPciConfig->DeviceID == NIC_PCI_DEVICE_ID_5 ) 
			)
        {
            DBGPRINT(MP_ERROR, ("VendorID/DeviceID don't match - %x/%x\n", 
                pPciConfig->VendorID, pPciConfig->DeviceID));

            ErrorCode = NDIS_ERROR_CODE_ADAPTER_NOT_FOUND;
            ErrorValue = ERRLOG_VENDOR_DEVICE_NOMATCH;

            break;
        }

        DBGPRINT(MP_INFO, ("Adapter is found - VendorID/DeviceID=%x/%x\n", 
            pPciConfig->VendorID, pPciConfig->DeviceID));

        // save info from config space
        Adapter->RevsionID = pPciConfig->RevisionID;
        Adapter->SubVendorID = pPciConfig->u.type0.SubVendorID;
        Adapter->SubSystemID = pPciConfig->u.type0.SubSystemID;

        //MpExtractPMInfoFromPciSpace (Adapter, (PUCHAR)pPciConfig);
        
        // --- HW_START   

        usPciCommand = pPciConfig->Command;
        if ((usPciCommand & PCI_ENABLE_WRITE_AND_INVALIDATE) && (Adapter->MWIEnable))
            Adapter->MWIEnable = TRUE;
        else
            Adapter->MWIEnable = FALSE;

        // --- HW_END

    
        if (resList == NULL)
        {
            ErrorCode = NDIS_ERROR_CODE_RESOURCE_CONFLICT;
            ErrorValue = ERRLOG_QUERY_ADAPTER_RESOURCES;
            break;
        }

        for (index=0; index < resList->Count; index++)
        {
            pResDesc = &resList->PartialDescriptors[index];

            switch(pResDesc->Type)
            {
                case CmResourceTypePort:
                    Adapter->IoBaseAddress = pResDesc->u.Port.Start ; 
                    Adapter->IoRange = pResDesc->u.Port.Length;
                    bResPort = TRUE;

                    DBGPRINT(MP_INFO, ("IoBaseAddress = 0x%x\n", Adapter->IoBaseAddress));
                    DBGPRINT(MP_INFO, ("IoRange = x%x\n", Adapter->IoRange));
                    break;

                case CmResourceTypeInterrupt:
                    Adapter->InterruptLevel = pResDesc->u.Interrupt.Level;
                    Adapter->InterruptVector = pResDesc->u.Interrupt.Vector;
                    bResInterrupt = TRUE;
                    
                    DBGPRINT(MP_INFO, ("InterruptLevel = x%x\n", Adapter->InterruptLevel));
                    break;

                case CmResourceTypeMemory:
                    // Our CSR memory space should be 0x1000, other memory is for 
                    // flash address, a boot ROM address, etc.
                    
					//if (pResDesc->u.Memory.Length == REALTEK_CSR_LENGTH )  //  changed
                    {
                        Adapter->MemPhysAddress = pResDesc->u.Memory.Start;
						Adapter->MemPhysLength = pResDesc->u.Memory.Length;

                        bResMemory = TRUE;
                        
                        DBGPRINT(MP_INFO, 
                            ("MemPhysAddress(Low) = 0x%0x\n", NdisGetPhysicalAddressLow(Adapter->MemPhysAddress)));
                        DBGPRINT(MP_INFO, 
                            ("MemPhysAddress(High) = 0x%0x\n", NdisGetPhysicalAddressHigh(Adapter->MemPhysAddress)));
                    }
                    break;
            }
        } 
        
        if (!bResPort || !bResInterrupt || !bResMemory)
        {
            Status = NDIS_STATUS_RESOURCE_CONFLICT;
            ErrorCode = NDIS_ERROR_CODE_RESOURCE_CONFLICT;
            
            if (!bResPort)
            {
                ErrorValue = ERRLOG_NO_IO_RESOURCE;
            }
            else if (!bResInterrupt)
            {
                ErrorValue = ERRLOG_NO_INTERRUPT_RESOURCE;
            }
            else 
            {
                ErrorValue = ERRLOG_NO_MEMORY_RESOURCE;
            }
            
            break;
        }
        
        Status = NDIS_STATUS_SUCCESS;

    } while (FALSE);
    
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisWriteErrorLogEntry(
            Adapter->AdapterHandle,
            ErrorCode,
            1,
            ErrorValue);
    }

    DBGPRINT_S(Status, ("<--- MpFindAdapter, Status=%x\n", Status));

    return Status;

}

NDIS_STATUS NICReadAdapterInfo(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Read the mac addresss from the adapter

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_INVALID_ADDRESS

--*/    
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    USHORT          usValue; 
    int             i;
	ULONG_PTR			ioaddr = Adapter->PortOffset ;

    DBGPRINT(MP_TRACE, ("--> NICReadAdapterInfo\n"));

    Adapter->EepromAddressSize = 0 ; 
        //GetEEpromAddressSize(GetEEpromSize(Adapter->PortOffset));
    DBGPRINT(MP_WARN, ("EepromAddressSize = %d\n", Adapter->EepromAddressSize));
        

	// Get MAC address //
	for (i = 0; i < ETH_LENGTH_OF_ADDRESS ; i++){
		Adapter->PermanentAddress[i] = RTL_R8( MAC0 + i );
	}

    DBGPRINT(MP_INFO, ("Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n", 
        Adapter->PermanentAddress[0], Adapter->PermanentAddress[1], 
        Adapter->PermanentAddress[2], Adapter->PermanentAddress[3], 
        Adapter->PermanentAddress[4], Adapter->PermanentAddress[5]));

    if (ETH_IS_MULTICAST(Adapter->PermanentAddress) || 
        ETH_IS_BROADCAST(Adapter->PermanentAddress))
    {
        DBGPRINT(MP_ERROR, ("Permanent address is invalid\n")); 

        NdisWriteErrorLogEntry(
            Adapter->AdapterHandle,
            NDIS_ERROR_CODE_NETWORK_ADDRESS,
            0);
        Status = NDIS_STATUS_INVALID_ADDRESS;         
    }
    else
    {
        if (!Adapter->bOverrideAddress)
        {
            ETH_COPY_NETWORK_ADDRESS(Adapter->CurrentAddress, Adapter->PermanentAddress);
        }

        DBGPRINT(MP_INFO, ("Current Address = %02x-%02x-%02x-%02x-%02x-%02x\n", 
            Adapter->CurrentAddress[0], Adapter->CurrentAddress[1],
            Adapter->CurrentAddress[2], Adapter->CurrentAddress[3],
            Adapter->CurrentAddress[4], Adapter->CurrentAddress[5]));
    }

    DBGPRINT_S(Status, ("<-- NICReadAdapterInfo, Status=%x\n", Status));

    return Status;
}

NDIS_STATUS MpAllocAdapterBlock(
    OUT PMP_ADAPTER     *pAdapter,
    IN  NDIS_HANDLE     MiniportAdapterHandle
    )
/*++
Routine Description:

    Allocate MP_ADAPTER data block and do some initialization

Arguments:

    Adapter     Pointer to receive pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_FAILURE

--*/    
{
    PMP_ADAPTER     Adapter;
    NDIS_STATUS     Status;

    DBGPRINT(MP_TRACE, ("--> NICAllocAdapter\n"));

    *pAdapter = NULL;

    do
    {
        // Allocate MP_ADAPTER block
        (PVOID)Adapter = MP_ALLOCMEMTAG(MiniportAdapterHandle, sizeof(MP_ADAPTER));
        if (Adapter == NULL)
        {
            Status = NDIS_STATUS_RESOURCES;
            DBGPRINT(MP_ERROR, ("Failed to allocate memory - ADAPTER\n"));
            break;
        }
        
        Status = NDIS_STATUS_SUCCESS;

        // Clean up the memory block
        NdisZeroMemory(Adapter, sizeof(MP_ADAPTER));

        MP_INC_REF(Adapter);

        //
        // Init lists, spinlocks, etc.
        // 
        InitializeQueueHeader(&Adapter->SendWaitQueue);
        InitializeQueueHeader(&Adapter->SendCancelQueue);
        //
        // PendingRequestQueue
        //
        InitializeQueueHeader(&Adapter->PendingRequestQueue);
        
        InitializeListHead(&Adapter->RecvList);
        InitializeListHead(&Adapter->RecvPendList);
        InitializeListHead(&Adapter->PoMgmt.PatternList);

        NdisInitializeEvent(&Adapter->ExitEvent);
        NdisInitializeEvent(&Adapter->AllPacketsReturnedEvent);

        NdisAllocateSpinLock(&Adapter->Lock);
        NdisAllocateSpinLock(&Adapter->SendLock);
        NdisAllocateSpinLock(&Adapter->RcvLock);

    } while (FALSE);

    *pAdapter = Adapter;

    DBGPRINT_S(Status, ("<-- NICAllocAdapter, Status=%x\n", Status));

    return Status;

}

VOID MpFreeAdapter(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Free all the resources and MP_ADAPTER data block

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    None                                                    

--*/    
{
    PMP_TXBUF       pMpTxBuf;
    PMP_RBD         pMpRbd;

    DBGPRINT(MP_TRACE, ("--> NICFreeAdapter\n"));

    // No active and waiting sends
    ASSERT(Adapter->nBusySend == 0);
    ASSERT(Adapter->nWaitSend == 0);
    ASSERT(IsQueueEmpty(&Adapter->SendWaitQueue));
    ASSERT(IsQueueEmpty(&Adapter->SendCancelQueue));

    // No other pending operations
    ASSERT(IsListEmpty(&Adapter->RecvPendList));
    ASSERT(Adapter->bAllocNewRecvBuffer == FALSE);
    ASSERT(MP_GET_REF(Adapter) == 0);

    //
    // Free hardware resources
    //      
    if (MP_TEST_FLAG(Adapter, fMP_ADAPTER_INTERRUPT_IN_USE))
    {
        NdisMDeregisterInterruptEx(Adapter->NdisInterruptHandle);
        MP_CLEAR_FLAG(Adapter, fMP_ADAPTER_INTERRUPT_IN_USE);
    }

    if (Adapter->CSRAddress)
    {
        NdisMUnmapIoSpace(
            Adapter->AdapterHandle,
            Adapter->CSRAddress,
            NIC_MAP_IOSPACE_LENGTH);
        Adapter->CSRAddress = NULL;
    }

    if (Adapter->PortOffset)
    {
        NdisMDeregisterIoPortRange(
            Adapter->AdapterHandle,
            NdisGetPhysicalAddressLow ( Adapter->IoBaseAddress ) ,
            Adapter->IoRange,
            (PVOID)Adapter->PortOffset);
        Adapter->PortOffset = 0;
    }

    //               
    // Free RECV memory/NDIS buffer/NDIS packets/shared memory
    //
    ASSERT(Adapter->nReadyRecv == Adapter->CurrNumRecvBuffer);

    while (!IsListEmpty(&Adapter->RecvList))
    {
        pMpRbd = (PMP_RBD)RemoveHeadList(&Adapter->RecvList);
        NICFreeRecvBuffer(Adapter, pMpRbd, FALSE);
    }
    //
    // Free the chunk of memory
    // 
    if (Adapter->HwRecvBuffers)
    {
        NdisMFreeSharedMemory(
            Adapter->AdapterHandle,
            Adapter->HwRecvBufferSize * Adapter->NumHwRecvBuffers,
            FALSE,
            Adapter->HwRecvBuffers,
            Adapter->HwRecvBuffersPa);
    }

	//new addition for realtek driver
	//
    // Free all REALTEK RECV_BUFFERs
    // 
    if (Adapter->HwRbds)
    {
        NdisMFreeSharedMemory(
            Adapter->AdapterHandle,
            Adapter->TotalSharedMemAllocatedForRbds,
            TRUE,					// FALSE,
            Adapter->HwRbds,
            Adapter->HwRbdsPa);
    }

    //
    // Free receive buffer pool
    // 
    if (Adapter->RecvBufferPool)
    {
        Adapter->RecvBufferPool = NULL;
    }

    //
    // Free receive packet pool
    // 
    if (Adapter->RecvNetBufferListPool)
    {
        NdisFreeNetBufferListPool(Adapter->RecvNetBufferListPool);
        Adapter->RecvNetBufferListPool = NULL;
    }
    
    if (MP_TEST_FLAG(Adapter, fMP_ADAPTER_RECV_LOOKASIDE))
    {
        NdisDeleteNPagedLookasideList(&Adapter->RecvLookaside);
        MP_CLEAR_FLAG(Adapter, fMP_ADAPTER_RECV_LOOKASIDE);
    }
            
    //               
    // Free SEND memory/NDIS buffer/NDIS packets/shared memory
    //
    while (!IsSListEmpty(&Adapter->SendBufList))
    {
        pMpTxBuf = (PMP_TXBUF)PopEntryList(&Adapter->SendBufList);
        ASSERT(pMpTxBuf);
            
        //
        // Free the NDIS buffer
        // 
        if (pMpTxBuf->Mdl)
        {
            NdisFreeMdl(pMpTxBuf->Mdl);
            pMpTxBuf->Mdl = NULL;
        }
    }

    if (Adapter->MpTxBufAllocVa != NULL)
    {
        NdisMFreeSharedMemory(
            Adapter->AdapterHandle,
            Adapter->MpTxBufAllocSize,
            TRUE,
            Adapter->MpTxBufAllocVa,
            Adapter->MpTxBufAllocPa);
    
        Adapter->MpTxBufAllocVa = NULL;
    }

    //
    // Free the send buffer pool
    // 
    if (Adapter->SendBufferPool)
    {
        Adapter->SendBufferPool = NULL;
    }

    //
    // Free the shared memory for HW_TBD structures
    // 
    if (Adapter->HwSendMemAllocVa)
    {
        NdisMFreeSharedMemory(
            Adapter->AdapterHandle,
            Adapter->HwSendMemAllocSize,
            FALSE,
            Adapter->HwSendMemAllocVa,
            Adapter->HwSendMemAllocPa);
        Adapter->HwSendMemAllocVa = NULL;
    }

    //
    // Free the shared memory for other command data structures                       
    // 
    if (Adapter->HwMiscMemAllocVa)
    {
        NdisMFreeSharedMemory(
            Adapter->AdapterHandle,
            Adapter->HwMiscMemAllocSize,
            FALSE,
            Adapter->HwMiscMemAllocVa,
            Adapter->HwMiscMemAllocPa);
        Adapter->HwMiscMemAllocVa = NULL;
    }

    //
    // Deregister the DMA handle after freeing all shared memory
    //
    if (Adapter->NdisMiniportDmaHandle != NULL)
    {
        NdisMDeregisterScatterGatherDma(Adapter->NdisMiniportDmaHandle);
    }
    Adapter->NdisMiniportDmaHandle = NULL;


    if (Adapter->pSGListMem)
    {
        MP_FREEMEM(Adapter->pSGListMem);
        Adapter->pSGListMem = NULL;
    }
    //
    // Free the memory for MP_TCB structures
    // 
    if (Adapter->MpTcbMem)
    {
        MP_FREEMEM(Adapter->MpTcbMem);
        Adapter->MpTcbMem = NULL;
    }

    //
    //Free all the wake up patterns on this adapter
    //
    MPRemoveAllWakeUpPatterns(Adapter);
    
    NdisFreeSpinLock(&Adapter->Lock);
    NdisFreeSpinLock(&Adapter->SendLock);
    NdisFreeSpinLock(&Adapter->RcvLock);    

    MP_FREEMEM(Adapter);  

#if DBG
    if (MPInitDone)
    {
        NdisFreeSpinLock(&MPMemoryLock);
    }
#endif

    DBGPRINT(MP_TRACE, ("<-- NICFreeAdapter\n"));
}

NDIS_STATUS
NICReadRegParameters(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Read the following from the registry
    1. All the parameters
    2. NetworkAddres

Arguments:

    Adapter                         Pointer to our adapter
    MiniportAdapterHandle           For use by NdisOpenConfiguration
    
Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_FAILURE
    NDIS_STATUS_RESOURCES                                       

--*/    
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    NDIS_HANDLE     ConfigurationHandle;
    PMP_REG_ENTRY   pRegEntry;
    UINT            i;
    UINT            value;
    PUCHAR          pointer;
    PNDIS_CONFIGURATION_PARAMETER ReturnedValue;
    PUCHAR          NetworkAddress;
    UINT            Length;
    NDIS_CONFIGURATION_OBJECT ConfigObject;

    DBGPRINT(MP_TRACE, ("--> NICReadRegParameters\n"));

    ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.Header.Size = sizeof(NDIS_CONFIGURATION_OBJECT);
    ConfigObject.NdisHandle = Adapter->AdapterHandle;
    ConfigObject.Flags = 0;

    Status = NdisOpenConfigurationEx(&ConfigObject,
                                     &ConfigurationHandle);
    
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DBGPRINT(MP_ERROR, ("NdisOpenConfiguration failed\n"));
        DBGPRINT_S(Status, ("<-- NICReadRegParameters, Status=%x\n", Status));
        return Status;
    }
    // read all the registry values 
    for (i = 0, pRegEntry = NICRegTable; i < NIC_NUM_REG_PARAMS; i++, pRegEntry++)
    {
        //
        // Driver should NOT fail the initialization only because it can not
        // read the registry
        //
#pragma prefast(suppress: __WARNING_POTENTIAL_BUFFER_OVERFLOW, "pRegEntry is bounded by the check above");        
        ASSERT(pRegEntry->bRequired == FALSE);                
        pointer = (PUCHAR) Adapter + pRegEntry->FieldOffset;

        DBGPRINT_UNICODE(MP_INFO, &pRegEntry->RegName);

        // Get the configuration value for a specific parameter.  Under NT the
        // parameters are all read in as DWORDs.
        NdisReadConfiguration(
            &Status,
            &ReturnedValue,
            ConfigurationHandle,
            &pRegEntry->RegName,
            NdisParameterInteger);

        // If the parameter was present, then check its value for validity.
        if (Status == NDIS_STATUS_SUCCESS)
        {
            // Check that param value is not too small or too large
            if (ReturnedValue->ParameterData.IntegerData < pRegEntry->Min ||
                ReturnedValue->ParameterData.IntegerData > pRegEntry->Max)
            {
                value = pRegEntry->Default;
            }
            else
            {
                value = ReturnedValue->ParameterData.IntegerData;
            }

            DBGPRINT_RAW(MP_INFO, ("= 0x%x\n", value));
        }
        else if (pRegEntry->bRequired)
        {
            DBGPRINT_RAW(MP_ERROR, (" -- failed\n"));

            ASSERT(FALSE);

            Status = NDIS_STATUS_FAILURE;
            break;
        }
        else
        {
            value = pRegEntry->Default;
            DBGPRINT_RAW(MP_INFO, ("= 0x%x (default)\n", value));
            Status = NDIS_STATUS_SUCCESS;
        }

        //
        // Store the value in the adapter structure.
        //
        switch(pRegEntry->FieldSize)
        {
            case 1:
                *((PUCHAR) pointer) = (UCHAR) value;
                break;

            case 2:
                *((PUSHORT) pointer) = (USHORT) value;
                break;

            case 4:
                *((PULONG) pointer) = (ULONG) value;
                break;

            default:
                DBGPRINT(MP_ERROR, ("Bogus field size %d\n", pRegEntry->FieldSize));
                break;
        }
    }

    // Read NetworkAddress registry value 
    // Use it as the current address if any
    if (Status == NDIS_STATUS_SUCCESS)
    {
        NdisReadNetworkAddress(
            &Status,
            &NetworkAddress,
            &Length,
            ConfigurationHandle);
        // If there is a NetworkAddress override in registry, use it 
        if ((Status == NDIS_STATUS_SUCCESS) && (Length == ETH_LENGTH_OF_ADDRESS))
        {
            if ((ETH_IS_MULTICAST(NetworkAddress) 
                    || ETH_IS_BROADCAST(NetworkAddress))
                    || !ETH_IS_LOCALLY_ADMINISTERED (NetworkAddress))
            {
                DBGPRINT(MP_ERROR, 
                    ("Overriding NetworkAddress is invalid - %02x-%02x-%02x-%02x-%02x-%02x\n", 
                    NetworkAddress[0], NetworkAddress[1], NetworkAddress[2],
                    NetworkAddress[3], NetworkAddress[4], NetworkAddress[5]));
            }
            else
            {
                ETH_COPY_NETWORK_ADDRESS(Adapter->CurrentAddress, NetworkAddress);
                Adapter->bOverrideAddress = TRUE;
            }
        }

        Status = NDIS_STATUS_SUCCESS;
    }

    // Close the registry
    NdisCloseConfiguration(ConfigurationHandle);
    
    // Decode SpeedDuplex
    if (Status == NDIS_STATUS_SUCCESS && Adapter->SpeedDuplex)
    {
        switch(Adapter->SpeedDuplex)
        {
            case 1:
            Adapter->AiTempSpeed = 10; Adapter->AiForceDpx = 1;
            break;
            
            case 2:
            Adapter->AiTempSpeed = 10; Adapter->AiForceDpx = 2;
            break;
            
            case 3:
            Adapter->AiTempSpeed = 100; Adapter->AiForceDpx = 1;
            break;
            
            case 4:
            Adapter->AiTempSpeed = 100; Adapter->AiForceDpx = 2;
            break;
        }
    
    }

	// //new additions for realtek driver. for optimum performance?
	Adapter->NumRecvBuffer = 512 + 256 ;  // 64

	//WARNING!!
	// DONT increase this. This is the maximum because of 128*8=1024 descriptors
	// //new additions for realtek driver. for optimum performance?
	Adapter->NumTcb = 128 ; 

    DBGPRINT_S(Status, ("<-- NICReadRegParameters, Status=%x\n", Status));

    return Status;
}

NDIS_STATUS 
NICAllocAdapterMemory(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Allocate all the memory blocks for send, receive and others

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_FAILURE
    NDIS_STATUS_RESOURCES

--*/    
{
    NDIS_STATUS     Status;
    PMP_TXBUF       pMpTxbuf;
    PUCHAR          pMem;
    LONG            index;
    ULONG           ErrorValue = 0;
    UINT            MaxNumBuffers;
    NDIS_SG_DMA_DESCRIPTION     DmaDescription;
    ULONG           pMemSize;
    PUCHAR          AllocVa;
	NDIS_PHYSICAL_ADDRESS   AllocPa, makeshiftPa={0,0} ;
    PVOID           MpTxBufMem;
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    ULONGLONG       TbdLength, SendBufLength;
    
    DBGPRINT(MP_TRACE, ("--> NICAllocMemory\n"));

    DBGPRINT(MP_INFO, ("NumTcb=%d\n", Adapter->NumTcb));
    
    do
    {

        NdisZeroMemory(&DmaDescription, sizeof(DmaDescription));

        DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
        DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
        DmaDescription.Header.Size = sizeof(NDIS_SG_DMA_DESCRIPTION);

        //DmaDescription.Flags = 0;       // we don't do 64 bit DMA
        DmaDescription.Flags =  NDIS_SG_DMA_64_BIT_ADDRESS ; 
        
        //
        // Even if offload is enabled, the packet size for mapping shouldn't change
        //
        DmaDescription.MaximumPhysicalMapping = NIC_MAX_RECV_FRAME_SIZE;
        
        DmaDescription.ProcessSGListHandler = MpProcessSGList;
        DmaDescription.SharedMemAllocateCompleteHandler = MPAllocateComplete;

        Status = NdisMRegisterScatterGatherDma(
                    Adapter->AdapterHandle,
                    &DmaDescription,
                    &Adapter->NdisMiniportDmaHandle);
                    
        if (Status == NDIS_STATUS_SUCCESS)
        {
            Adapter->ScatterGatherListSize = DmaDescription.ScatterGatherListSize;
            MP_SET_FLAG(Adapter, fMP_ADAPTER_SCATTER_GATHER);
        }
        else
        {
            DBGPRINT(MP_WARN, ("Failed to init ScatterGather DMA\n"));

            //
            // NDIS 6.0 miniport should NOT use map register
            //
            ErrorValue = ERRLOG_OUT_OF_SG_RESOURCES;
                     
            break;  
        }
        //
        // Send + Misc
        //

        //
		//NdisAllocateMdl
        // Allocate MP_TCB's
        //         
        Adapter->MpTcbMemSize = Adapter->NumTcb * (sizeof(MP_TCB) + sizeof(MP_TXBUF));

        //
        // Integer overflow test
        //
        if ((Adapter->NumTcb <= 0) || 
            ((sizeof(MP_TCB) + sizeof(MP_TXBUF)) < sizeof(MP_TCB)))
        {
            Status = NDIS_STATUS_RESOURCES;
            ErrorValue = ERRLOG_OUT_OF_MEMORY;
            DBGPRINT(MP_ERROR, ("Integer overflow caused by invalid NumTcb\n"));
            break;                
        }

        TbdLength = (ULONGLONG)Adapter->NumTcb * (ULONGLONG)(sizeof(MP_TCB) + sizeof(MP_TXBUF));        
        if (TbdLength > (ULONG)-1)            
        {
            Status = NDIS_STATUS_RESOURCES;
            ErrorValue = ERRLOG_OUT_OF_MEMORY;
            DBGPRINT(MP_ERROR, ("Integer overflow caused by invalid NumTcb\n"));
            break;        
        }

        pMem = MP_ALLOCMEMTAG(Adapter->AdapterHandle, Adapter->MpTcbMemSize);
        if (pMem == NULL)
        {
            Status = NDIS_STATUS_RESOURCES;
            ErrorValue = ERRLOG_OUT_OF_MEMORY;
            DBGPRINT(MP_ERROR, ("Failed to allocate MP_TCB's\n"));
            break;
        }
        NdisZeroMemory(pMem, Adapter->MpTcbMemSize);
        Adapter->MpTcbMem = pMem;
        MpTxBufMem = pMem + (Adapter->NumTcb * sizeof(MP_TCB));
        

        //
        // Allocate memory for scatter-gather list
        // Integer overflow check
        //
        if (!NT_SUCCESS(RtlULongMult(Adapter->NumTcb,
                                     Adapter->ScatterGatherListSize,
                                     &pMemSize)))
        {
            Status = NDIS_STATUS_RESOURCES;                
            ErrorValue = ERRLOG_OUT_OF_MEMORY;
            DBGPRINT(MP_ERROR, ("Integer overflow while allocating Scatter Gather list\n"));
            break;
        }

        pMem = MP_ALLOCMEMTAG(Adapter->AdapterHandle, pMemSize);
        
        if (pMem == NULL)
        {
            Status = NDIS_STATUS_RESOURCES;                
            ErrorValue = ERRLOG_OUT_OF_MEMORY;
            DBGPRINT(MP_ERROR, ("Failed to allocate Scatter Gather list buffer\n"));
            break;
        }
        NdisZeroMemory(pMem, pMemSize);
        Adapter->pSGListMem = pMem;

        //
        // Allocate send buffers
        //

        pMpTxbuf = (PMP_TXBUF)MpTxBufMem;         

        //Adapter->CacheFillSize = NdisMGetDmaAlignment(Adapter->AdapterHandle);
        Adapter->CacheFillSize = 16;  
        DBGPRINT(MP_INFO, ("CacheFillSize=%d\n", Adapter->CacheFillSize));

        Adapter->MpTxBufAllocSize = Adapter->NumTcb * 
			( NIC_MAX_XMIT_FRAME_SIZE + Adapter->CacheFillSize );

        // 
        // Integer overflow check
        //
        if ((NIC_MAX_XMIT_FRAME_SIZE + Adapter->CacheFillSize) <
            NIC_MAX_XMIT_FRAME_SIZE)
        {
            ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
            DBGPRINT(MP_ERROR, ("Integer overflow while allocating send buffers\n"));
            Status = NDIS_STATUS_RESOURCES;
            break;        
        }

        SendBufLength = (ULONGLONG)Adapter->NumTcb * 
                        (ULONGLONG) (NIC_MAX_XMIT_FRAME_SIZE + Adapter->CacheFillSize);
        if (SendBufLength > (ULONG)-1)
        {
            ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
            DBGPRINT(MP_ERROR, ("Integer overflow while allocating send buffers\n"));
            Status = NDIS_STATUS_RESOURCES;
            break;        
        }
        
        NdisMAllocateSharedMemory(
                        Adapter->AdapterHandle,
                        Adapter->MpTxBufAllocSize,
                        TRUE,                           // CACHED
                        &Adapter->MpTxBufAllocVa,  
                        &Adapter->MpTxBufAllocPa);

        if (Adapter->MpTxBufAllocVa == NULL)
        {
            ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
            DBGPRINT(MP_ERROR, ("Failed to allocate a big buffer\n"));
            Status = NDIS_STATUS_RESOURCES;
            break;
        }
        
        AllocVa = Adapter->MpTxBufAllocVa;
        AllocPa = Adapter->MpTxBufAllocPa;
        
        for (index = 0; index < Adapter->NumTcb; index++)
        {
            pMpTxbuf->AllocSize = NIC_MAX_XMIT_FRAME_SIZE + Adapter->CacheFillSize;
            pMpTxbuf->BufferSize = NIC_MAX_XMIT_FRAME_SIZE ;
            pMpTxbuf->AllocVa = AllocVa;
            pMpTxbuf->AllocPa = AllocPa;
            //
            // Align the buffer on the cache line boundary
            //
            pMpTxbuf->pBuffer = MP_ALIGNMEM(pMpTxbuf->AllocVa, Adapter->CacheFillSize);
            pMpTxbuf->BufferPa.QuadPart = MP_ALIGNMEM_PA(pMpTxbuf->AllocPa, Adapter->CacheFillSize);

            pMpTxbuf->Mdl = NdisAllocateMdl(Adapter->AdapterHandle, pMpTxbuf->pBuffer, pMpTxbuf->BufferSize);

            if (pMpTxbuf->Mdl == NULL)
            {
                ErrorValue = ERRLOG_OUT_OF_NDIS_BUFFER;
                DBGPRINT(MP_ERROR, ("Failed to allocate MDL for a big buffer\n"));
                break;
            }

            PushEntryList(&Adapter->SendBufList, &pMpTxbuf->SList);

            pMpTxbuf++;
            AllocVa += NIC_MAX_XMIT_FRAME_SIZE;
            AllocPa.QuadPart += NIC_MAX_XMIT_FRAME_SIZE; 
        }

        if (Status != NDIS_STATUS_SUCCESS) 
            break;

        // HW_START

        //
        // Allocate shared memory for send and integer overflow check
        //
        if (!NT_SUCCESS(RtlULongMult(Adapter->NumTcb, 
                     ( NIC_MAX_PHYS_BUF_COUNT * sizeof(TBD_STRUC)),
                     &Adapter->HwSendMemAllocSize)))
        {
            Status = NDIS_STATUS_RESOURCES;                
            ErrorValue = ERRLOG_OUT_OF_MEMORY;
            DBGPRINT(MP_ERROR, ("Integer overflow while allocating shared memory for send\n"));
            break;
        }

		
		Adapter->HwSendMemAllocSize += 256 ;
		Adapter->nTbds = NIC_MAX_PHYS_BUF_COUNT * Adapter->NumTcb ;
		ASSERT ( Adapter->nTbds <= REALTEK_MAX_TBD ) ;

        NdisMAllocateSharedMemory(
            Adapter->AdapterHandle,
            Adapter->HwSendMemAllocSize,
            FALSE, 
            (PVOID) &Adapter->HwSendMemAllocVa,
            &Adapter->HwSendMemAllocPa);

        if (!Adapter->HwSendMemAllocVa)
        {
            ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
            DBGPRINT(MP_ERROR, ("Failed to allocate send memory\n"));
            Status = NDIS_STATUS_RESOURCES;
            break;
        }

        NdisZeroMemory(Adapter->HwSendMemAllocVa, Adapter->HwSendMemAllocSize);

        //
        // Allocate shared memory for other uses
        //
        Adapter->HwMiscMemAllocSize =
            sizeof(ERR_COUNT_STRUC) + ALIGN_64;

        //
        // Allocate the shared memory for the command block data structures.
        //
        NdisMAllocateSharedMemory(
            Adapter->AdapterHandle,
            Adapter->HwMiscMemAllocSize,
            TRUE,  // FALSE,
            (PVOID *) &Adapter->HwMiscMemAllocVa,
            &Adapter->HwMiscMemAllocPa);
        if (!Adapter->HwMiscMemAllocVa)
        {
            ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
            DBGPRINT(MP_ERROR, ("Failed to allocate misc memory\n"));
            Status = NDIS_STATUS_RESOURCES;
            break;
        }


        NdisZeroMemory(Adapter->HwMiscMemAllocVa, Adapter->HwMiscMemAllocSize);

        pMem = Adapter->HwMiscMemAllocVa; 

		makeshiftPa = Adapter->HwMiscMemAllocPa ;
        Adapter->StatsCounters = (PERR_COUNT_STRUC)MP_ALIGNMEM(pMem, ALIGN_64);
        Adapter->StatsCounterPhys.QuadPart = MP_ALIGNMEM_PA(makeshiftPa, ALIGN_64);

        // HW_END

        //
        // Recv
        //

        NdisInitializeNPagedLookasideList(
            &Adapter->RecvLookaside,
            NULL,
            NULL,
            0,
            sizeof(MP_RBD),
            NIC_TAG, 
            0);

        MP_SET_FLAG(Adapter, fMP_ADAPTER_RECV_LOOKASIDE);

        //
        // set the max number of RECV_BUFFERs
        // disable the RECV_BUFFER grow/shrink scheme if user specifies a NumRecvBuffer value 
        // larger than NIC_MAX_GROW_RECV_BUFFERS
        // 
        Adapter->MaxNumRecvBuffer = max(Adapter->NumRecvBuffer, NIC_MAX_GROW_RECV_BUFFERS);

        //
        // The driver should allocate more data than sizeof(RBUF_STRUC) to allow the
        // driver to align the data(after ethernet header) at 8 byte boundary
        //
		Adapter->HwRecvBufferSize = sizeof(HW_RECV_BUFFER) + 
								MORE_DATA_FOR_8_ALIGN ;

		Adapter->HwRbdSize = sizeof( HW_RECV_BUFFER ) ;  


        //
        // alloc the recv net buffer list pool with net buffer inside the list
        //
        NdisZeroMemory(&PoolParameters, sizeof(NET_BUFFER_LIST_POOL_PARAMETERS));
        PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        PoolParameters.Header.Size = sizeof(PoolParameters);
        PoolParameters.ProtocolId = 0;
        PoolParameters.ContextSize = 0;
        PoolParameters.fAllocateNetBuffer = TRUE;
        PoolParameters.PoolTag = NIC_TAG;

        Adapter->RecvNetBufferListPool = NdisAllocateNetBufferListPool(
                    MP_GET_ADAPTER_HANDLE(Adapter),
                    &PoolParameters); 


        if (Adapter->RecvNetBufferListPool == NULL)
        {
            ErrorValue = ERRLOG_OUT_OF_PACKET_POOL;
            break;
        }
        
        
        Status = NDIS_STATUS_SUCCESS;

    } while (FALSE);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisWriteErrorLogEntry(
            Adapter->AdapterHandle,
            NDIS_ERROR_CODE_OUT_OF_RESOURCES,
            1,
            ErrorValue);
    }
    DBGPRINT_S(Status, ("<-- NICAllocMemory, Status=%x\n", Status));

    return Status;

}

VOID NICInitSend(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Initialize send data structures

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    None                                                    

--*/    
{
    PMP_TCB         pMpTcb;
    LONG            TcbCount;
    PMP_TXBUF       pMpTxBuf;
    PTBD_STRUC      pHwTbd;  
    
	NDIS_PHYSICAL_ADDRESS       HwTbdPa;     

    DBGPRINT(MP_TRACE, ("--> NICInitSend\n"));

    //
    // Setup the initial pointers to the SW and HW TBD data space
    //
    pMpTcb = (PMP_TCB) Adapter->MpTcbMem;
    pMpTxBuf = (PMP_TXBUF) (pMpTcb + Adapter->NumTcb);

    // Setup the initial pointers to the TBD data space.
    // TBDs are located immediately following the TBDs
    pHwTbd = (PTBD_STRUC) (Adapter->HwSendMemAllocVa );
    HwTbdPa = Adapter->HwSendMemAllocPa ;

	/*********		//new additions for realtek driver			**************
	***	for realtek gigabit 256 byte align requirement for the base of
	receive and xmit descriptors		
	********************************************************/
	Adapter->HwTbdBase  = (PTBD_STRUC)
		MP_ALIGNMEM( (PUCHAR)pHwTbd, MORE_DATA_FOR_256_ALIGN ) ;

	Adapter->HwTbdBasePa.QuadPart = 
		MP_ALIGNMEM_PA ( HwTbdPa, MORE_DATA_FOR_256_ALIGN  ) ;

	pHwTbd = Adapter->HwTbdBase ;

	////new additions for realtek driver
    NdisZeroMemory(Adapter->HwSendMemAllocVa, Adapter->HwSendMemAllocSize);


    // Go through and set up each TCB
    for (TcbCount = 0; TcbCount < Adapter->NumTcb; TcbCount++)
    {

        pMpTcb->MpTxBuf = pMpTxBuf;

		////new addition for realtek driver
		// if last TCB set EOR bit
		if ( TcbCount == Adapter->NumTcb-1 )	{
			( pHwTbd + ( NIC_MAX_PHYS_BUF_COUNT - 1 ))->status |= (USHORT)(EORbit>>16) ;
		}

        ASSERT(Adapter->pSGListMem != NULL);
        pMpTcb->ScatterGatherListBuffer = Adapter->pSGListMem + 
					TcbCount * (LONG) Adapter->ScatterGatherListSize;
        
        //
        // Link TCBs together
		//
        if (TcbCount < Adapter->NumTcb - 1)
        {
            pMpTcb->Next = pMpTcb + 1;
        }
        else  // if last TCB
        {
            pMpTcb->Next = (PMP_TCB) Adapter->MpTcbMem;
        }
        if (TcbCount)  // any other than first TCB
        {
            pMpTcb->Prev = pMpTcb - 1;
        }
        else  // first TCB
        {
            pMpTcb->Prev = (PMP_TCB)((PUCHAR)Adapter->MpTcbMem + ((Adapter->NumTcb - 1) * sizeof(MP_TCB)));
        }
          
		// loop around
        pMpTcb++; 
        pMpTxBuf++;
        pHwTbd += NIC_MAX_PHYS_BUF_COUNT ;


	}

    // set the TBD head/tail indexes
    // head is the olded one to free, tail is the next one to use
    Adapter->CurrSendHead = (PMP_TCB) Adapter->MpTcbMem;
    Adapter->CurrSendTail = (PMP_TCB) Adapter->MpTcbMem;

	Adapter->NextFreeTbd = Adapter->HwTbdBase ;

    DBGPRINT(MP_TRACE, ("<-- NICInitSend\n"));
}

NDIS_STATUS NICInitRecv(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Initialize receive data structures

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_RESOURCES

--*/    
{
	NDIS_STATUS             Status = NDIS_STATUS_RESOURCES;

	PMP_RBD				pMpRbd;   
	PHW_RBD				HwRbd;
	int                 RecvBufferCount;
	ULONG                   ErrorValue = 0;
	PUCHAR                  HwRecvBuffers;
	NDIS_PHYSICAL_ADDRESS   HwRecvBuffersPa;
	ULONG                   ulLength;
	
	DBGPRINT(MP_TRACE, ("--> NICInitRecv\n"));

	do
	{
		/***  //new additions for realtek driver ***
		
		- allocate memory for some of 1024 realtek RECV_BUFFERs, NOT intel RFds
		// which are done below. 
						***/
		
		//
		// Compute total size of realtek HW RECV_BUFFERs
		//
		if (FAILED(ULongMult(Adapter->HwRbdSize, \
								Adapter->NumRecvBuffer, \
								&Adapter->TotalSharedMemAllocatedForRbds)))
		{
			Adapter->HwRbds = NULL;
			break;
		}
		Adapter->TotalSharedMemAllocatedForRbds += MORE_DATA_FOR_256_ALIGN ;

		NdisMAllocateSharedMemory(
			Adapter->AdapterHandle,
			Adapter->TotalSharedMemAllocatedForRbds,
			FALSE,
			&Adapter->HwRbds,
			&Adapter->HwRbdsPa);
		
		if ( ! Adapter->HwRbds )
		{
			ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
			break;
		}
		
        Adapter->HwRbdBase = MP_ALIGNMEM( Adapter->HwRbds, MORE_DATA_FOR_256_ALIGN );
        Adapter->HwRbdBasePa.QuadPart = 
			MP_ALIGNMEM_PA( Adapter->HwRbdsPa, MORE_DATA_FOR_256_ALIGN );
		
		Adapter->EarlyFreeRbdHint = 0 ;

		//
		// Allocate shared memory for all the RECV_BUFFERs
		// If the allocation fails, try a smaller size
		//
		for (RecvBufferCount = Adapter->NumRecvBuffer; RecvBufferCount > NIC_MIN_RECV_BUFFERS; RecvBufferCount >>= 1)
		{
			//
			// Compute total size of HW RECV_BUFFERs
			//
			if (FAILED(ULongMult(Adapter->HwRecvBufferSize, RecvBufferCount, &ulLength)))
			{
				Adapter->HwRecvBuffers = NULL;
				break;
			}

			NdisMAllocateSharedMemory(
				Adapter->AdapterHandle,
				ulLength,
				TRUE,					// FALSE,
				&Adapter->HwRecvBuffers,
				&Adapter->HwRecvBuffersPa);
			if (Adapter->HwRecvBuffers)
			{
				break;
			}
		}
		Adapter->NumHwRecvBuffers = RecvBufferCount;

		if (!Adapter->HwRecvBuffers )
		{
			ErrorValue = ERRLOG_OUT_OF_SHARED_MEMORY;
			break;
		}
		// Zero all entries in the array of MP_RBDs
		ZeroOutMpRbdsArray ( Adapter ) ;

		//
		// Setup each RECV_BUFFER
		// 
		HwRecvBuffers =  Adapter->HwRecvBuffers;
		HwRecvBuffersPa = Adapter->HwRecvBuffersPa;
		for (HwRbd = (PHW_RBD)Adapter->HwRbdBase, RecvBufferCount = 0; 
			RecvBufferCount < Adapter->NumHwRecvBuffers; RecvBufferCount++, ++HwRbd )
		{
			
			NdisZeroMemory ( HwRbd , sizeof ( HW_RBD ) ) ;
			
			// if last RECV_BUFFER descriptor
			if ( RecvBufferCount ==  Adapter->NumHwRecvBuffers - 1)	{
				HwRbd->status = EORbit ;
			}

			// Allocate memory for MP_RBD
			pMpRbd = NdisAllocateFromNPagedLookasideList(&Adapter->RecvLookaside);
			if (!pMpRbd)
			{
				ErrorValue = ERRLOG_OUT_OF_LOOKASIDE_MEMORY;
				continue;
			}
			// and Zero it
			NdisZeroMemory ( pMpRbd, sizeof ( MP_RBD ) ) ;

            //
            // Allocate the shared memory for this RECV_BUFFER.
            // 

            //
            // Get a 8-byts aligned memory from the original HwRecvBuffer
            //
            pMpRbd->HwRecvBuffer = (PHW_RECV_BUFFER)DATA_ALIGN(HwRecvBuffers);
        

			//
            // Update physical address accordingly
            // 
            pMpRbd->HwRecvBufferPa.QuadPart = HwRecvBuffersPa.QuadPart + 
								BYTES_SHIFT(pMpRbd->HwRecvBuffer, HwRecvBuffers);
            
            ErrorValue = NICAllocRBD ( Adapter, pMpRbd );
            if (ErrorValue)
            {
                NdisFreeToNPagedLookasideList(&Adapter->RecvLookaside, pMpRbd);
                continue;
            }

			//
			// Add this RECV_BUFFER to the RecvList
			// 
			Adapter->CurrNumRecvBuffer++;                      
			HwRecvBuffers += Adapter->HwRecvBufferSize;
			HwRecvBuffersPa.QuadPart += Adapter->HwRecvBufferSize;

			MP_SET_FLAG(pMpRbd, fMP_RBD_ALLOC_FROM_MEM_CHUNK);

			NICReturnRBD(Adapter, pMpRbd);
		}
	} while (FALSE);

	if (Adapter->CurrNumRecvBuffer > NIC_MIN_RECV_BUFFERS)
	{
		Status = NDIS_STATUS_SUCCESS;
	}

	Adapter->CurrentRBD = 0;
	//
	// Adapter->CurrNumRecvBuffer < NIC_MIN_RECV_BUFFERs
	//
	if (Status != NDIS_STATUS_SUCCESS)
	{
		NdisWriteErrorLogEntry(
			Adapter->AdapterHandle,
			NDIS_ERROR_CODE_OUT_OF_RESOURCES,
			1,
			ErrorValue);
	}

	DBGPRINT_S(Status, ("<-- NICInitRecv, Status=%x\n", Status));

	return Status;
}

ULONG NICAllocRBD(
    IN  PMP_ADAPTER     Adapter,
    IN  PMP_RBD         pMpRbd
    )
/*++
Routine Description:

    Allocate NET_BUFFER_LIST, NET_BUFFER and MDL associated with a RECV_BUFFER

Arguments:

    Adapter     Pointer to our adapter
    pMpRbd      pointer to a RECV_BUFFER

Return Value:

    ERRLOG_OUT_OF_NDIS_PACKET
    ERRLOG_OUT_OF_NDIS_BUFFER

--*/    
{
    NDIS_STATUS         Status;
    PHW_RECV_BUFFER             pHwRecvBuffer;   
    ULONG               HwRecvBufferPhys;  
    ULONG               ErrorValue = 0;

    do
    {
        pHwRecvBuffer = pMpRbd->HwRecvBuffer;

        pMpRbd->Flags = 0;
        pMpRbd->NetBufferList = NULL;
        pMpRbd->Mdl = NULL;
        
        //
        // point our buffer for receives at this RecvBuffer
        // 
        pMpRbd->Mdl = NdisAllocateMdl(Adapter->AdapterHandle, 
						(PVOID)&pHwRecvBuffer->RecvBuffer, 
						sizeof(pHwRecvBuffer->RecvBuffer));
        
        if (pMpRbd->Mdl == NULL)
        {
            ErrorValue = ERRLOG_OUT_OF_NDIS_BUFFER;
            break;
        }
        //
        // the NetBufferList may change later(RequestControlSize is not enough)
        //
        pMpRbd->NetBufferList = 
			NdisAllocateNetBufferAndNetBufferList(
                        Adapter->RecvNetBufferListPool,
                        0,                      // RequestContolOffsetDelta
                        0,                      // BackFill Size
                        pMpRbd->Mdl,			// MdlChain
                        0,                      // DataOffset
                        0);                     // DataLength

        if (pMpRbd->NetBufferList == NULL)
        {
            ErrorValue = ERRLOG_OUT_OF_NDIS_PACKET;
            break;
        }
        pMpRbd->NetBufferList->SourceHandle = MP_GET_ADAPTER_HANDLE(Adapter);

	    //
        // Initialize the NetBufferList
        //
        // Save ptr to MP_RBD in the NBL, used in MPReturnNetBufferLists 
        // 
        MP_GET_NET_BUFFER_LIST_RECV_BUFFER(pMpRbd->NetBufferList) = pMpRbd;


    } while (FALSE);

    if (ErrorValue)
    {
        if (pMpRbd->Mdl)
        {
            NdisFreeMdl(pMpRbd->Mdl);
        }
        //
        // Do not free the shared memory
        //

    }

    return ErrorValue;

}

VOID NICFreeRecvBuffer(
    IN  PMP_ADAPTER     Adapter,
    IN  PMP_RBD         pMpRbd,
    IN  BOOLEAN         DispatchLevel
    )
/*++
Routine Description:

    Free a RECV_BUFFER and assocaited NET_BUFFER_LIST

Arguments:

    Adapter         Pointer to our adapter
    pMpRbd          Pointer to a RECV_BUFFER
    DisptchLevel    Specify if the caller is at DISPATCH level

Return Value:

    None                                                    

--*/    
{
    ASSERT(pMpRbd->Mdl);      
    ASSERT(pMpRbd->NetBufferList);  
    ASSERT(pMpRbd->HwRecvBuffer);    

    NdisFreeMdl(pMpRbd->Mdl);
    NdisFreeNetBufferList(pMpRbd->NetBufferList);
    
    pMpRbd->Mdl = NULL;
    pMpRbd->NetBufferList = NULL;

    //
    // Free HwRecvBuffer, we need to free the original memory pointed by OriginalHwRecvBuffer.
    // 
    if (!MP_TEST_FLAG(pMpRbd, fMP_RBD_ALLOC_FROM_MEM_CHUNK))
    {
        NdisMFreeSharedMemory(
            Adapter->AdapterHandle,
            Adapter->HwRecvBufferSize,
            TRUE,
            pMpRbd->OriginalHwRecvBuffer,
            pMpRbd->OriginalHwRecvBufferPa);
    }

    pMpRbd->HwRecvBuffer = NULL;
    pMpRbd->OriginalHwRecvBuffer = NULL;
	pMpRbd->HwRbd = NULL ;

    NdisFreeToNPagedLookasideList(&Adapter->RecvLookaside, pMpRbd);
}


NDIS_STATUS NICSelfTest(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Perform a NIC self-test

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_DEVICE_FAILED

--*/    
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    ULONG           SelfTestCommandCode;
	int retval=0;

    DBGPRINT(MP_TRACE, ("--> NICSelfTest\n"));

 
    //
    // Issue a software reset to the adapter
    //
    retval = HwSoftwareReset(Adapter);

    //
    if ( retval  == 0 )
    {
        DBGPRINT(MP_ERROR, ("CHIP sw reset failed"));

        NdisWriteErrorLogEntry(
            Adapter->AdapterHandle,
            NDIS_ERROR_CODE_HARDWARE_FAILURE,
            1,
            ERRLOG_SELFTEST_FAILED);

        Status = NDIS_STATUS_DEVICE_FAILED;
    }

    DBGPRINT_S(Status, ("<-- NICSelfTest, Status=%x\n", Status));

    return Status;
}

// SW chip reset already done at this time
NDIS_STATUS NICconfigureAndInitializeAdapter(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Initialize the adapter and set up everything
// SW chip reset already done at this time

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_HARD_ERRORS

--*/    
{
    NDIS_STATUS     Status;

    DBGPRINT(MP_TRACE, ("--> NICconfigureAndInitializeAdapter\n"));

    do
    {

        // set up our link indication variable
        // it doesn't matter what this is right now because it will be
        // set correctly if link fails
        Adapter->MediaState = MediaConnectStateUnknown;
        Adapter->MediaDuplexState = MediaDuplexStateUnknown;
        Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
            
        Adapter->CurrentPowerState = NdisDeviceStateD0;
        Adapter->NextPowerState    = NdisDeviceStateD0;

		// SW chip reset already done at this time

		// Load the TBD BASE 
		RealtekHwCommand( Adapter, SCB_TBD_LOAD_BASE, FALSE);
		// Load the RECV_BUFFER BASE 
		RealtekHwCommand( Adapter, SCB_RUC_LOAD_BASE, FALSE);

        // Configure the adapter
        Status = HwConfigure(Adapter);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            break;
        }

        HwClearAllCounters(Adapter);

    } while (FALSE);
    
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisWriteErrorLogEntry(
            Adapter->AdapterHandle,
            NDIS_ERROR_CODE_HARDWARE_FAILURE,
            1,
            ERRLOG_INITIALIZE_ADAPTER);
    }

    DBGPRINT_S(Status, ("<-- NICconfigureAndInitializeAdapter, Status=%x\n", Status));

    return Status;
}    


HwSoftwareReset(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    Issue a software reset to the hardware    

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    None                                                    

--*/    
{
	int i=0;
	UINT8 tmp8 =0 ;

	ULONG_PTR ioaddr=Adapter->PortOffset;

    DBGPRINT(MP_TRACE, ("--> HwSoftwareReset\n"));

	// Soft reset the chip.
	RTL_W8 ( ChipCmd, CmdReset);

	// Check that the chip has finished the reset.
	for (i = 1000; i > 0; i--){
		if ( (RTL_R8(ChipCmd) & CmdReset) == 0){
			break;
		}
		else{
			NdisStallExecution ( NIC_DELAY_POST_RESET );
		}
	}

	if ( 0 == i )	{
		DBGPRINT(MP_WARN, ("Chip sw reset failed\n"));
		return 0 ;
	}

    // wait after the port reset command
    NdisStallExecution(NIC_DELAY_POST_RESET);

	/* disable Tx and Rx till things settle down */
	NIC_DISABLE_RECEIVER ;
	NIC_DISABLE_XMIT ( Adapter ) ;

    // Mask off our interrupt line -- its unmasked after reset
    NICDisableInterrupt(Adapter);

    DBGPRINT(MP_TRACE, ("<-- HwSoftwareReset\n"));

	return 1 ;
}

// SW chip reset already done at this time. ints disabled and xmit recv disabled
// and RBD and TBD base registers loaded w/ meaningful values
NDIS_STATUS HwConfigure(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

// SW chip reset already done at this time
// and RBD and TBD base registers loaded w/ meaningful values

    Configure the hardware    

Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_HARD_ERRORS

--*/    
{
    NDIS_STATUS         Status;
    UINT                i;

    DBGPRINT(MP_TRACE, ("--> HwConfigure\n"));

    //
    // Init the packet filter to nothing.
    // 
    Adapter->OldPacketFilter = Adapter->PacketFilter;
    Adapter->PacketFilter = 0;

	//At this point...
	// state of the NIC HW :: SW reset done and INTs disabled. Rx & Tx units disabled
	// realtek RBD & TBD base address registers loaded w/ meaningful values
	if ( r1000_init_one ( Adapter ) != -1 )
		Status = NDIS_STATUS_SUCCESS ;
	else
		Status = NDIS_STATUS_HARD_ERRORS ;

    DBGPRINT_S(Status, ("<-- HwConfigure, Status=%x\n", Status));

    return Status;
}

NDIS_STATUS HwClearAllCounters(
    IN  PMP_ADAPTER     Adapter
    )
/*++
Routine Description:

    This routine will clear the hardware error statistic counters
    
Arguments:

    Adapter     Pointer to our adapter

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_HARD_ERRORS

--*/    
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS ;
    BOOLEAN         bResult;
	ULONG_PTR		ioaddr = Adapter->PortOffset ;

    DBGPRINT(MP_TRACE, ("--> HwClearAllCounters\n"));

    do
    {

        // Issue the load dump counters address command
        RealtekHwCommand(Adapter, SCB_CUC_DUMP_ADDR, FALSE);

        // Now dump and reset all of the statistics
        RealtekHwCommand(Adapter, SCB_CUC_DUMP_RST_STAT, TRUE);

        // Now wait for the dump/reset to complete, timeout value 2 secs
        MP_STALL_AND_WAIT( \
			( (RTL_R8 ( DumpTally) & 8 ) == 0 ), 2000, bResult);

        // init packet counts
        Adapter->GoodTransmits = 0;
        Adapter->GoodReceives = 0;

        // init transmit error counts
        Adapter->TxAbortExcessCollisions = 0;
        Adapter->TxLateCollisions = 0;
        Adapter->TxDmaUnderrun = 0;
        Adapter->TxLostCRS = 0;
        Adapter->TxOKButDeferred = 0;
        Adapter->OneRetry = 0;
        Adapter->MoreThanOneRetry = 0;
        Adapter->TotalRetries = 0;

        // init receive error counts
        Adapter->RcvCrcErrors = 0;
        Adapter->RcvAlignmentErrors = 0;
        Adapter->RcvResourceErrors = 0;
        Adapter->RcvDmaOverrunErrors = 0;
        Adapter->RcvCdtFrames = 0;
        Adapter->RcvRuntErrors = 0;

    } while (FALSE);

    DBGPRINT_S(Status, ("<-- HwClearAllCounters, Status=%x\n", Status));

    return Status;
}

// Initialize/Zero-out RBD and set OWN bit to push
// ownership of RBDs to NIC HW
void
ZeroRBDandDisOwnToNIC ( 
		PHW_RBD pHwRbd,
		PMP_RBD pMpRbd
		)
{
	ULONG	saveEORbit ;

	saveEORbit = pHwRbd->status & EORbit ;  
	NdisZeroMemory ( pHwRbd , sizeof ( HW_RBD ) ) ;
	pHwRbd->status = NIC_MAX_RECV_FRAME_SIZE ;
	pHwRbd->status |= saveEORbit ;

	pHwRbd->buf_Haddr = NdisGetPhysicalAddressHigh(pMpRbd->HwRecvBufferPa)  ;
	
	pHwRbd->buf_addr =  NdisGetPhysicalAddressLow(pMpRbd->HwRecvBufferPa) 
							+ FIELD_OFFSET(HW_RECV_BUFFER,RecvBuffer ) ;

	pHwRbd->status |= OWNbit  ;
	
}

// READ a BYTE from the Realtek1G config space
void pci_write_config_byte(  int offset, UCHAR value , PMP_ADAPTER Adapter)
{
	NdisMSetBusData(
		Adapter->AdapterHandle,
		PCI_WHICHSPACE_CONFIG,
		offset,
		&value,
		sizeof(value) );

}


