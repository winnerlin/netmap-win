
//-----------------------------------------------------------------------------
// Procedure:   RealtekHwCommand
//
// Description: This general routine used to touch some device registers
		// in the realtek 1G.
//
// Arguments:
//      Adapter - ptr to Adapter object instance.
//      ScbCommand - The command that is to be issued
//      WaitForSCB - NOT USED
//
// Returns: none

//-----------------------------------------------------------------------------
__inline void 
RealtekHwCommand(
    IN PMP_ADAPTER Adapter,
    IN UCHAR ScbCommandLow,
    IN BOOLEAN NotUsed
    )
{
	ULONG_PTR ioaddr = Adapter->PortOffset ;

	switch ( ScbCommandLow )	{
		
		case SCB_RUC_START :
	
			NIC_ENABLE_RECEIVER ;
			break ;

		case SCB_CUC_DUMP_ADDR :
			RTL_W32 ( DumpTally, \
						NdisGetPhysicalAddressLow ( Adapter->StatsCounterPhys)  )  ;
			RTL_W32 ( DumpTally+4, 
				 NdisGetPhysicalAddressHigh ( Adapter->StatsCounterPhys) ) ;
			break;
		case SCB_CUC_DUMP_RST_STAT :
			RTL_W32 ( DumpTally+4, 
				 NdisGetPhysicalAddressHigh ( Adapter->StatsCounterPhys)  ) ;
			RTL_W32 ( DumpTally, \
						NdisGetPhysicalAddressLow ( Adapter->StatsCounterPhys) | 8  )  ;
			break ;

		case SCB_TBD_LOAD_BASE :
			RTL_W32 ( TxDescStartAddr, \
				  NdisGetPhysicalAddressLow ( Adapter->HwTbdBasePa ) );
			RTL_W32 ( TxDescStartAddr + 4, \
				NdisGetPhysicalAddressHigh ( Adapter->HwTbdBasePa ) );
			break ;

		case SCB_XMIT_START :
		case SCB_XMIT_RESUME :
			{
				NIC_ENABLE_XMIT ( Adapter ) ;
				NIC_START_NORMAL_POLLING ( Adapter ) ; //set normal xmit polling bit

				break ;
			}

		case SCB_RUC_LOAD_BASE :
			RTL_W32 ( RxDescStartAddr, \
				NdisGetPhysicalAddressLow ( Adapter->HwRbdBasePa ) );
			RTL_W32 ( RxDescStartAddr + 4, \
				NdisGetPhysicalAddressHigh ( Adapter->HwRbdBasePa ));

		case SCB_RUC_ABORT :
			NIC_DISABLE_RECEIVER ;

			break ;

			
	}
}

// routines.c           

VOID
DumpStatsCounters(
    IN PMP_ADAPTER Adapter
    );

NDIS_MEDIA_CONNECT_STATE 
NICGetMediaState(
    IN PMP_ADAPTER Adapter
    );

VOID
NICIssueSelectiveReset(
    IN PMP_ADAPTER Adapter
    );

VOID
NICIssueFullReset(
    IN PMP_ADAPTER Adapter
    );

// PHY function declarations

VOID
ResetPhy(
    IN PMP_ADAPTER Adapter
    );
