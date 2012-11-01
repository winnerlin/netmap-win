/****************************************************************************
** COPYRIGHT (C) 1994-1997 INTEL CORPORATION                               **
** DEVELOPED FOR MICROSOFT BY INTEL CORP., HILLSBORO, OREGON               **
** HTTP://WWW.INTEL.COM/                                                   **
** THIS FILE IS PART OF THE INTEL ETHEREXPRESS PRO/100B(TM) AND            **
** ETHEREXPRESS PRO/100+(TM) NDIS 5.0 MINIPORT SAMPLE DRIVER               **
****************************************************************************/

/****************************************************************************
Module Name:
    routines.c

This driver runs on the following hardware:
    - realtek 1Gig ethernet adapters

Environment:
    Kernel Mode - Or whatever is the equivalent on WinNT

Revision History
*****************************************************************************/

#include "precomp.h"
#pragma hdrstop
#pragma warning (disable: 4514 4706)



//-----------------------------------------------------------------------------
// Procedure:   DumpStatsCounters
//
// Description: This routine will dump and reset the 821G's internal
//              Statistics counters.  The current stats dump values will be
//              added to the "Adapter's" overall statistics.
// Arguments:
//      Adapter - ptr to Adapter object instance
//
// Returns:
//      NOTHING
//-----------------------------------------------------------------------------
VOID DumpStatsCounters(
    IN PMP_ADAPTER Adapter)
{
    BOOLEAN bResult;
	PUCHAR ioaddr = (PUCHAR)Adapter->PortOffset ;

    // The query is for a driver statistic, so we need to first
    // update our statistics in software.
    
	NdisAcquireSpinLock(&Adapter->Lock);
    // Dump and reset the hardware's statistic counters
    RealtekHwCommand(Adapter, SCB_CUC_DUMP_RST_STAT, TRUE);
    NdisReleaseSpinLock(&Adapter->Lock);

	// Now wait for the dump/reset to complete, timeout value 2 secs
	MP_STALL_AND_WAIT( \
		(  (RTL_R8 ( DumpTally ) & 8)  == 0 ), 2000, bResult);

	if (!bResult)
	{
		MP_SET_HARDWARE_ERROR(Adapter);
		DBGPRINT(MP_WARN, ("DumpTally FAILED\n"));
	}

    // Output the debug counters to the debug terminal.
    DBGPRINT(MP_INFO, ("Good Transmits %d\n", Adapter->StatsCounters->XmtGoodFrames));
    DBGPRINT(MP_INFO, ("Good Receives %d\n", Adapter->StatsCounters->RcvGoodFrames));
    DBGPRINT(MP_INFO, ("Transmit Underruns %d\n", Adapter->StatsCounters->XmtUnderruns));
    DBGPRINT(MP_INFO, ("One Collision xmits %d\n", Adapter->StatsCounters->XmtSingleCollision));
    DBGPRINT(MP_INFO, ("Total Collisions %d\n", Adapter->StatsCounters->XmtTotalCollisions));

    DBGPRINT(MP_INFO, ("Receive Alignment errors %d\n", Adapter->StatsCounters->RcvAlignmentErrors));
    DBGPRINT(MP_INFO, ("Receive overrun errors %d\n", Adapter->StatsCounters->RcvOverrunErrors));

    // update packet counts
    Adapter->GoodTransmits = Adapter->StatsCounters->XmtGoodFrames;
    Adapter->GoodReceives = Adapter->StatsCounters->RcvGoodFrames;

    // update transmit error counts
    Adapter->TxAbortExcessCollisions = Adapter->StatsCounters->XmtTotalCollisions;
    Adapter->TxLateCollisions = Adapter->StatsCounters->XmtTotalCollisions;
    Adapter->TxDmaUnderrun = Adapter->StatsCounters->XmtUnderruns;
    Adapter->TxLostCRS = Adapter->StatsCounters->TxAbt;
    Adapter->OneRetry = Adapter->StatsCounters->XmtSingleCollision;
    Adapter->MoreThanOneRetry = Adapter->StatsCounters->XmtTotalCollisions;
    Adapter->TotalRetries = Adapter->StatsCounters->XmtTotalCollisions;

    // update receive error counts
    Adapter->RcvAlignmentErrors = Adapter->StatsCounters->RcvAlignmentErrors;
    Adapter->RcvDmaOverrunErrors = Adapter->StatsCounters->RcvOverrunErrors;
}


//-----------------------------------------------------------------------------
// Procedure:   NICIssueSelectiveReset
//
// Description: This routine will issue a selective reset, forcing the adapter
//              the CU and RU back into their idle states.  The receive unit
//              will then be re-enabled if it was previously enabled, because
//              an RNR interrupt will be generated when we abort the RU.
//
// Arguments:
//      Adapter - ptr to Adapter object instance
//
// Returns:
//      NOTHING
//-----------------------------------------------------------------------------

VOID NICIssueSelectiveReset(
    PMP_ADAPTER Adapter)
{
    NDIS_STATUS     Status;
    BOOLEAN         bResult;


	HwSoftwareReset ( Adapter ) ;

	// disable interrupts after issuing reset, because the int
	// line gets raised when reset completes.
	NICDisableInterrupt(Adapter);

  
}

VOID NICIssueFullReset(
    PMP_ADAPTER Adapter)
{
    BOOLEAN     bResult;

    NICIssueSelectiveReset(Adapter);

    NICDisableInterrupt(Adapter);
	

}

//-----------------------------------------------------------------------------
// Procedure: GetConnectionStatus
//
// Description: This function returns the connection status that is
//              a required indication for PC 97 specification from MS
//              the value we are looking for is if there is link to the
//              wire or not.
//
// Arguments: IN Adapter structure pointer
//
// Returns:   NdisMediaStateConnected
//            NdisMediaStateDisconnected
//-----------------------------------------------------------------------------
NDIS_MEDIA_CONNECT_STATE 
NICGetMediaState(
    IN PMP_ADAPTER Adapter
    )
{
	ULONG_PTR ioaddr = Adapter->PortOffset ;

	return ( NIC_LINK_UP ? MediaConnectStateConnected : MediaConnectStateDisconnected ) ;
		
}

