
/** The only new module in the DDK NDIS sample written from scratch
***/

#include "precomp.h"

/**	

terminology :
	RBD# - This is any one of the 1024 realtek Receive buffer descriptors 
			in the host memory
			shared by the (cpu/host)driver s/w and device h/w or firmware
		
	MP_RBD - This is the driver-centric concept of a receive buffer descriptor and it
		is actually a struct MP_RBD defined in mp.h. This holds lot of book keeping
		info other than the RBD#. This is S/W ONLY struture not touched by
		NIC h/w anytime ( it is not even aware of this structure )
		All the MP_RBDs are linked in a list
	
The challenge :
	The intel NIC for which this DDK sample was originally written, had RBDs
	in a linked-list and the NIC h/w itself was able to handle this. 
	So it was convenient to
	have even the MP_RBDs in a linked list, and each MP_RBD was a associated
	with a single RBD# ( MP_RBD->HwRbd ). 
	
	But realtek NICs has an array of consecutive RBDs. So, either we discard
	the linked list structures pf MP_RBDs or have an additional object which holds 
	MP_RBDs in a consecutive fashion, but each entry corresponds to any of the element
	in the linked list of MP_RBDs. And that object is 
	Adapter->pMpRbds [ NIC_MAX_RECV_BUFFERS ]. The index of this array
	acts as the RBD# e.g array[5]=MP_RBD means this MP_RBD is linked to the h/w RBD# 5
	in the NIC ring descriptor

	Also, because of the asynchronous
	nature of indicating received NBLs to NDIS and NDIS returning the NBLS later by
	(MP)MiniportReturnNetbufferLists(.,.,) , order cannot be guaranteed for freeing
	or reassigning MP_RBD -> RBD#

	So, there is a possibility that there may be GAPs in the array whereby some RBDs
	are still owned by the CPU and not NIC h/w through the OWN bit.
	BUt realtek NIC doesnt seem to be robust enough to handle this. It STOPs its 
	receive unit if it encounters such a GAP ( OWN bit clear ). 
	When the gap is filled later ( OWN bit set ), 
	even a "enable receive" command
	by setting bit 3 in device register 0x37 , does NOT bring the receive unit
	back to life. It is STALLED for ever waiting for a reset.
	side note --- A zero write to 0x37 , 2 ms delay, enable receiver and transmitter
				sequence seems to kick the receive unit back to life, EXACTLY at the
				RBD# where it stopped earlier and NOT at RBD# = 0 . Good, 
				though this is not an elegant solution to our problem

The solution :
	Have an additional Adapter->pMpRbds [ NIC_MAX_RECV_BUFFERS ] array. Each entry
	corresponds to a RBD# in the realtek HW receive descriptor ring. Each MP_RBD structure
	is mapped to any of these RBDs if the slot is free ( NULL ). After the 
	receive interrupt the corresponding entry in Adapter->pMpRbds[x] is NULLified.
	and this MP_RBD is removed from the linked list. 
	When NDIS calls MpReturnNBLs(.,,) later ,  
	returns this NBL, a new free slot is searched for this MP_RBD in NICreturnRBD(.,.,,,)
	function

	Every time a MP_RBD is returned to the driver by NDIS 
	through MiniportReturnNetbufferLists(.,.,)  
	Adapter->EarlyFreeRbdHint is used to track the RBD# last allocated to an MP_RBD
	So that to find a free slot
	next time, instead of starting the search from "0 to LAST_RBD" in the ring
	it goes "EarlyFreeRbdHint to EarlyFreeRbdHint -1". So in effect
	Adapter->EarlyFreeRbdHint denotes the last allocated RBD# to an MP_RBD.

	Remember, that even though MP_RBDs is a linked-list structure and handled
	as such in many places ( remove/insert head/tail ), each one ends up in 
	the Adapter->pMpRbds [ NIC_MAX_RECV_BUFFERS ] array. The index of this array
	acts as the RBD# e.g array[5]=MP_RBD means this MP_RBD is linked to the h/w RBD# 5
	in the NIC ring descriptor
	
	To start with all entries are NULL. NULL signifies , not used
	before the receive unit is started.


*/

//
// get the next RBD in a safe way
// taking into consideration realtek receive and transmit descriptors
// wrap around to form a ring
//
_inline 
int
INC_RBD_INDEX ( PMP_ADAPTER A,
		 int index 
		 )
{
	return ( ++index >= A->NumHwRecvBuffers ? 0 : index ) ;
}

// Find a free slot and insert the MP_RBD
NDIS_STATUS
InsertMpRbdInFreeHwRbdsArray (
	IN	PMP_ADAPTER		Adapter,
	IN	PMP_RBD			pMpRbd 
	)
{
	int i=0, cnt=0 ;

	for (	cnt = 0, i = Adapter->EarlyFreeRbdHint  ; 
			cnt < Adapter->NumHwRecvBuffers ;
			++ cnt )
	{
		if ( NULL == Adapter->pMpRbds[i] )	{
			Adapter->pMpRbds [ i ] = pMpRbd ;
			pMpRbd->HwRbd = ((PHW_RBD)Adapter->HwRbdBase) + i ; 
			
			// update early bird
			Adapter->EarlyFreeRbdHint = INC_RBD_INDEX ( Adapter, i ) ; 
			return NDIS_STATUS_SUCCESS;
		}
		// get next RBD#
		i = INC_RBD_INDEX ( Adapter, i ) ; 
	}
	
	//there must be a free NIC-completed RBD available 
	ASSERT ( cnt != Adapter->NumHwRecvBuffers ) ;

	return NDIS_STATUS_FAILURE ;
}

// Get the MP_RBD for a corresponding h/w RBD#
NDIS_STATUS
GetMpRbd (
	IN	PMP_ADAPTER         Adapter,
	IN	int CurrentRBD ,
	OUT	PMP_RBD *pMpRbd 
	)
{
	*pMpRbd = Adapter->pMpRbds [ CurrentRBD ] ;
	return NDIS_STATUS_SUCCESS ;
}

// implicit
NDIS_STATUS
ZeroOutMpRbdsArray (
	IN	PMP_ADAPTER		Adapter
	)
{
	int cnt=0 ;

	for (	cnt = 0  ; 
			cnt < Adapter->NumHwRecvBuffers ;
			++ cnt )
	{
		Adapter->pMpRbds[cnt] = NULL ;
	}

	return NDIS_STATUS_SUCCESS ;
}

