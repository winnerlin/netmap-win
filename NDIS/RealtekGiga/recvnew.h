
#ifndef _RECV_NEW_H
#define _RECV_NEW_H

NDIS_STATUS
InsertMpRbdInFreeHwRbdsArray (
	IN	PMP_ADAPTER         Adapter,
	IN	PMP_RBD pMpRbd 
	) ;

NDIS_STATUS
GetMpRbd (
	IN	PMP_ADAPTER         Adapter,
	IN	int CurrentRBD ,
	OUT	PMP_RBD *pMpRbd 
	) ;

NDIS_STATUS
ZeroOutMpRbdsArray (
	IN	PMP_ADAPTER		Adapter
	);


#endif
