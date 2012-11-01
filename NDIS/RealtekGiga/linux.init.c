#include "precomp.h"
#include "linux_h.h"


struct r1000_private private_s, *priv = &private_s ;

static const unsigned int r1000_rx_config = (RX_FIFO_THRESH << RxCfgFIFOShift) | (RX_DMA_BURST << RxCfgDMAShift) | 0x0000000E;

void pci_write_config_byte (  int offset , unsigned char value , \
			PMP_ADAPTER Adapter ) ;

void pci_write_config_word (  int offset, USHORT value , \
			PMP_ADAPTER Adapter ) ;



//=================================================================
//	PHYAR
//	bit		Symbol
//	31		Flag
//	30-21	reserved
//	20-16	5-bit GMII/MII register address
//	15-0	16-bit GMII/MII register data
//=================================================================

// takes a maximum of 10 ms
void RTL8169_WRITE_GMII_REG( ULONG_PTR ioaddr, int RegAddr, int value )
{
	UINT32	i ;
	
	i = (	PHYAR_Write | 
			((RegAddr & PHYAR_Reg_Mask ) << PHYAR_Reg_shift ) |
			(value & PHYAR_Data_Mask)
		);

	RTL_W32 ( PHYAR, i );

	for( i = 100 ; i > 0 ; i -- ){
		// Check if the RTL8169 has completed writing to the specified MII register
		if( ! ( RTL_R32 ( PHYAR ) & PHYAR_Flag ) ) {
			break;
		}
		else{
			udelay(100);
		}// end of if( ! (RTL_R32(PHYAR)&0x80000000) )
	}// end of for() loop
#if DBG
	if ( !i )
		DBGPRINT( MP_INFO, \
		( "Rltkbx : RTL8169_WRITE_GMII_REG failed\n"));
#endif
}

// takes a maximum of 10 ms
int 
RTL8169_READ_GMII_REG (
				ULONG_PTR ioaddr, 
				int RegAddr
)
{
	int i, value = -1;

	RTL_W32(PHYAR, 
		PHYAR_Read | (RegAddr & PHYAR_Reg_Mask) << PHYAR_Reg_shift);

	for (i = 100; i > 0; --i ) {
		/* Check if the RTL8168 has completed retrieving data from the specified MII register */
		if (RTL_R32(PHYAR) & PHYAR_Flag) {
			value = (int) (RTL_R32(PHYAR) & PHYAR_Data_Mask);
			break;
		}
		udelay(100);
	}
#if DBG
	if ( !i )
		DBGPRINT( MP_INFO, \
		( "Rltkbx : RTL8169_READ_GMII_REG failed\n"));
#endif	
	return value;
}


/*** FUNCTIONS to read/write PCI config space		***/
void pci_write_config_word(  int offset, USHORT value , PMP_ADAPTER Adapter)
{
	NdisMSetBusData(
		Adapter->AdapterHandle,
		PCI_WHICHSPACE_CONFIG,
		offset,
		&value,
		sizeof(value) );

}

void pci_read_config_byte(  int offset, PUCHAR value, PMP_ADAPTER Adapter )
{
	NdisMGetBusData(
		Adapter->AdapterHandle,
		PCI_WHICHSPACE_CONFIG,
		offset,
		&value,
		sizeof(UCHAR) );

}

void pci_read_config_word( int offset, PUSHORT value, PMP_ADAPTER Adapter )
{
	NdisMGetBusData(
		Adapter->AdapterHandle,
		PCI_WHICHSPACE_CONFIG,
		offset,
		&value,
		sizeof(USHORT) );

}


void r1000_set_rx_mode ( PMP_ADAPTER A )
{
	unsigned long flags;
	u32 mc_filter[2]={0xFFFFFFFF,0xFFFFFFFF};	/* Multicast hash filter */
	int i, rx_mode;
	u32 tmp=0;
	ULONG_PTR ioaddr = A->PortOffset ;
	
	// Rx packet filter sent through adapter object
	rx_mode = A->OldParameterField ;
	
	/*****  setup MAC address filter  ***/
	if ( rx_mode & NDIS_PACKET_TYPE_PROMISCUOUS ) {

		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys | AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	
	} else if ( rx_mode & NDIS_PACKET_TYPE_MULTICAST || 
				rx_mode & NDIS_PACKET_TYPE_ALL_MULTICAST ) {

		rx_mode = AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
	
	} else if ( rx_mode & NDIS_PACKET_TYPE_DIRECTED ) {
		mc_filter[1] = mc_filter[0] = 0;
		rx_mode = AcceptMyPhys;
	}
	else {  // last resort. May be the initial state where
			// Adapter->OldParameterField is not set and equals ZERO
		rx_mode = AcceptBroadcast | AcceptMyPhys ;
	}

	/*** write this packet filter into receive configuration register at 0x44   ***/
	tmp = r1000_rx_config | rx_mode | 
				(RTL_R32(RxConfig) & rtl_chip_info[priv->chipset].RxConfigMask);
	RTL_W32 ( RxConfig, tmp);
		

	// fill-up MARs based on config method
	if((priv->mcfg==MCFG_METHOD_11)||(priv->mcfg==MCFG_METHOD_12)||
	   (priv->mcfg==MCFG_METHOD_13)||(priv->mcfg==MCFG_METHOD_14)||
	   (priv->mcfg==MCFG_METHOD_15)){
		RTL_W32 ( MAR0 + 0, 0xFFFFFFFF);
		RTL_W32 ( MAR0 + 4, 0xFFFFFFFF);
	}else{
		RTL_W32 ( MAR0 + 0, mc_filter[0]);
		RTL_W32 ( MAR0 + 4, mc_filter[1]);
	}

}//end of r1000_set_rx_mode 


static int r1000_hw_MAC_config ( PMP_ADAPTER A )
{
	u32 i;
	u8 i8;
	u16 i16;
	u8 device_control;
	ULONG_PTR ioaddr = (ULONG)A->PortOffset;

	/****  Common Configuration irrespective of chip# or revisions	*****/
	
	// UNlock config0-5 registers write
	RTL_W8 ( Cfg9346, Cfg9346_Unlock);

	// chip specific configuration starts
	if((priv->mcfg!=MCFG_METHOD_5)&&(priv->mcfg!=MCFG_METHOD_11)&&
	   (priv->mcfg!=MCFG_METHOD_12)&&(priv->mcfg!=MCFG_METHOD_13)&&
	   (priv->mcfg!=MCFG_METHOD_14)&&(priv->mcfg!=MCFG_METHOD_15)) {

	   /***	Realtek Data sheets recommend that er configure C+ Command rgister
	   **	first before we configure anything else. But the linux driver source
	   **	source code from realtek web site does things differently. Let us see
	   **	which one works		***************/

		// Configure C+ Command register at 0xE0
		RTL_W16( CPlusCmd, RTL_R16(CPlusCmd) );
		if(priv->mcfg==MCFG_METHOD_2||priv->mcfg==MCFG_METHOD_3){
			RTL_W16( CPlusCmd, (RTL_R16(CPlusCmd)|(1<<14)|(1<<3)) );
		}else{
			RTL_W16( CPlusCmd, (RTL_R16(CPlusCmd)|(1<<3)) );
		}

		if( priv->mcfg == MCFG_METHOD_2 ){
			DBGPRINT( MP_LOUD,("Set MAC Reg C+CR Offset 0x82h = 0x01h\n"));
			// Looks like a reserved reg ?
			RTL_W8( 0x82, 0x01 );
		}
		
		// tinker w/ PCI space and C+CR register
		if( priv->mcfg < MCFG_METHOD_3 ){
			pci_write_config_byte( PCI_LATENCY_TIMER, 0x40, A );
			DBGPRINT( MP_LOUD,("Set PCI Latency=0x40\n") );
		}		
		
		// Touching a RESERVED device register ?
		RTL_W16(0xE2,0x0000);
		

	} else {

		// Configure C+ Command register at 0xE0
		RTL_W16( CPlusCmd, RTL_R16(CPlusCmd) );
		if(priv->mcfg==MCFG_METHOD_2||priv->mcfg==MCFG_METHOD_3){
			RTL_W16( CPlusCmd, (RTL_R16(CPlusCmd)|(1<<14)|(1<<3)) );
		}else{
			RTL_W16( CPlusCmd, (RTL_R16(CPlusCmd)|(1<<3)) );
		}
		
		if (priv->mcfg == MCFG_METHOD_16) {
			
			// VPD data
			RTL_W32(0x64, 0x27000000);
			RTL_W32(0x68, 0x8000870C);
					
			// Touching RESERVED device registerS ?
			RTL_W8(0xD1, 0x38);
			RTL_W8(DBG_reg, 0x20);

			/*Increase the Tx performance*/
			pci_read_config_byte( 0x79, &device_control , A );
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte( 0x79, device_control , A );

			//ConfigX register w/o UNLOCKing ?
			RTL_W8(Config3, ~(1 << 0));
			RTL_W8(Config1, 0xDF);

		}

		if( priv->mcfg == MCFG_METHOD_13 ){
			// tinker w/ PCI space
			pci_write_config_word(0x68,0x00,A);
			pci_write_config_word(0x69,0x08,A);
		}

		if( priv->mcfg == MCFG_METHOD_5 ){
			
			//Config2 register w/o UNLOCKing ?
			// looks like some fix
			i8=RTL_R8(Config2);
			i8=i8&0x07;
			if(i8&&0x01)
				RTL_W32(0xff7C,0x0007FFFF);
	
			// Meddle w/ PCI space
			pci_read_config_word(0x04,&i16,A);
			i16=i16&0xEF;
			pci_write_config_word(0x04,i16,A);
		}

		// Touching a RESERVED device registerS ?
		RTL_W16(0xE2,0x0000);
	}
	
	/****  Common ConfigurationS irrespective of chip# or revisions	*****/
	
	/**** temporarily enable Tx & Rx units for configuration	****/
	// strange that TX has to be enabled for chip config ?!
	NIC_ENABLE_XMIT ( A ) ;
	NIC_ENABLE_RECEIVER ;

	// set MAX transmit and Receive packet size
	RTL_W8 ( ETThReg, ETTh);
	RTL_W16	( RxMaxSize, NIC_MAX_RECV_FRAME_SIZE );
	
	// Set Rx Config register
	i = r1000_rx_config | ( RTL_R32( RxConfig ) & rtl_chip_info[priv->chipset].RxConfigMask);

	/* Set DMA burst size and Interframe Gap Time */
	RTL_W32 ( TxConfig, (TX_DMA_BURST << TxDMAShift) | \
		(InterFrameGap << TxInterFrameGapShift) );
	
	// some statistics. who cares?
	RTL_W32 ( RxMissed, 0 );

	// set a default packet filter ( broadcast )
	r1000_set_rx_mode(A);

	// UNdocumented !?
	RTL_W16 ( MultiIntr, RTL_R16(MultiIntr) & 0xF000);

	//LOCK it back - config0-5 registers write
	RTL_W8 ( Cfg9346, Cfg9346_Lock );
	udelay (10);
	
	return 0 ;

}//end of r1000_hw_MAC_config 


static int r1000_set_speed_duplex( ULONG_PTR ioaddr, unsigned int anar, 
								  unsigned int gbcr, unsigned int bmcr){
	unsigned int i = 0;
	unsigned int bmsr;

	RTL8169_WRITE_GMII_REG(ioaddr,PHY_AUTO_NEGO_REG,anar);
	RTL8169_WRITE_GMII_REG(ioaddr,PHY_1000_CTRL_REG,gbcr);
	
	RTL8169_WRITE_GMII_REG(ioaddr,PHY_CTRL_REG,bmcr);

	/***	
	***		DONT wait for auto negotiation to complete   ***

	It may take up to 3 seconds
	
	for(i=0;i<100;i++){
		bmsr = RTL8169_READ_GMII_REG(ioaddr,PHY_STAT_REG);
		if(bmsr&PHY_Auto_Neco_Comp)
			return 0;
	}
	
	***/	
	
	return 0 ;
}


int r1000_set_medium( u16 speed,u8 duplex,u8 autoneg, PMP_ADAPTER A){

	ULONG_PTR ioaddr = (unsigned long)A->PortOffset;

	unsigned int anar=0,gbcr=0,bmcr=0,ret=0,val=0;

	val = RTL8169_READ_GMII_REG( ioaddr, PHY_AUTO_NEGO_REG ) ; 

#ifdef R1000_HW_FLOW_CONTROL_SUPPORT
	val |= PHY_Cap_PAUSE | PHY_Cap_ASYM_PAUSE ;
#endif //end #define R1000_HW_FLOW_CONTROL_SUPPORT

	bmcr = PHY_Restart_Auto_Nego|PHY_Enable_Auto_Nego;

	if(autoneg==AUTONEG_ENABLE){
		anar = PHY_Cap_10_Half|PHY_Cap_10_Full|PHY_Cap_100_Half|PHY_Cap_100_Full;
		gbcr = PHY_Cap_1000_Half|PHY_Cap_1000_Full;
	}else{
		if(speed==SPEED_1000){
			anar = PHY_Cap_10_Half|PHY_Cap_10_Full|PHY_Cap_100_Half|PHY_Cap_100_Full;
			if((priv->mcfg==MCFG_METHOD_13)||(priv->mcfg==MCFG_METHOD_14)||(priv->mcfg==MCFG_METHOD_15))
				gbcr = PHY_Cap_Null;
			else
				gbcr = PHY_Cap_1000_Half|PHY_Cap_1000_Full;
		}else if((speed==SPEED_100)&&(duplex==DUPLEX_FULL)){

			anar = PHY_Cap_10_Half|PHY_Cap_10_Full|PHY_Cap_100_Half|PHY_Cap_100_Full;
			gbcr = PHY_Cap_Null;
		}else if((speed==SPEED_100)&&(duplex==DUPLEX_HALF)){

			anar = PHY_Cap_10_Half|PHY_Cap_10_Full|PHY_Cap_100_Half;
			gbcr = PHY_Cap_Null;
		}else if((speed==SPEED_10)&&(duplex==DUPLEX_FULL)){

			anar = PHY_Cap_10_Half|PHY_Cap_10_Full;
			gbcr = PHY_Cap_Null;
		}else if((speed==SPEED_10)&&(duplex==DUPLEX_HALF)){

			anar = PHY_Cap_10_Half;
			gbcr = PHY_Cap_Null;
		}else{

			anar = PHY_Cap_10_Half|PHY_Cap_10_Full|PHY_Cap_100_Half|PHY_Cap_100_Full;
			gbcr = PHY_Cap_Null;
		}
	}

	//enable flow control
	anar |=  val&0xC1F;

	ret = r1000_set_speed_duplex(ioaddr,anar,gbcr,bmcr);
	
	return	ret;
}

// PHY configuration
static void r1000_hw_PHY_config ( PMP_ADAPTER Adapter )
{
	ULONG_PTR ioaddr = Adapter->PortOffset ;

	DBGPRINT( MP_INFO, \
		("priv->mcfg=%d, priv->pcfg=%d\n",priv->mcfg,priv->pcfg));

	if( priv->mcfg == MCFG_METHOD_4 ){
		RTL8169_WRITE_GMII_REG( ioaddr, 0x1F, 0x0002 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0x90D0 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x1F, 0x0000 );
	}else if((priv->mcfg == MCFG_METHOD_2)||(priv->mcfg == MCFG_METHOD_3)){
		RTL8169_WRITE_GMII_REG( ioaddr, 0x1f, 0x0001 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x06, 0x006e );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x08, 0x0708 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x15, 0x4000 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x18, 0x65c7 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x1f, 0x0001 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x03, 0x00a1 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x02, 0x0008 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0x0120 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0x1000 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0x0800 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0x0000 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x03, 0xff41 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x02, 0xdf60 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0x0140 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0x0077 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0x7800 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0x7000 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x03, 0x802f );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x02, 0x4f02 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0x0409 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0xf0f9 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0x9800 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0x9000 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x03, 0xdf01 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x02, 0xdf20 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0xff95 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0xba00 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xa800 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xa000 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x03, 0xff41 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x02, 0xdf20 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0x0140 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0x00bb );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xb800 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xb000 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x03, 0xdf41 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x02, 0xdc60 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0x6340 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0x007d );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xd800 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xd000 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x03, 0xdf01 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x02, 0xdf20 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x01, 0x100a );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0xa0ff );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xf800 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x04, 0xf000 );

		RTL8169_WRITE_GMII_REG( ioaddr, 0x1f, 0x0000 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x0b, 0x0000 );
		RTL8169_WRITE_GMII_REG( ioaddr, 0x00, 0x9200 );
	} else if(priv->mcfg == MCFG_METHOD_16) {
		RTL8169_WRITE_GMII_REG(ioaddr, 0x1F, 0x0002);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x00, 0x88D4);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x01, 0x82B1);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x03, 0x7002);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x08, 0x9E30);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x09, 0x01F0);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x0A, 0x5500);

		RTL8169_WRITE_GMII_REG(ioaddr, 0x1F, 0x0003);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x12, 0xC096);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x16, 0x000A);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x1f, 0x0000);
	
	} else if (priv->mcfg == MCFG_METHOD_12 ) {
		RTL8169_WRITE_GMII_REG(ioaddr, 0x1F, 0x0001);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x10, 0xF41B);
		RTL8169_WRITE_GMII_REG(ioaddr, 0x1F, 0x0000);
	} 
	else {
		DBGPRINT( MP_INFO, \
			("Discard Realtek1G PHY config\n"));
	}
}


// find chip & revision specific info
static int r1000_find_config_method ( PMP_ADAPTER Adapter )
{
	ULONG_PTR ioaddr = 0;
	int i;

	ioaddr = (ULONG)Adapter->PortOffset;

	// identify config method
	{
		unsigned long val32 = (RTL_R32(TxConfig)&0x7c800000);

		if(val32 == 0x3C000000)
			priv->mcfg = MCFG_METHOD_16;
		else if(val32 == 0x38800000)
			priv->mcfg = MCFG_METHOD_15;
		else if(val32 == 0x30800000)
			priv->mcfg = MCFG_METHOD_14;
		else if(val32 == 0x34000000)
			priv->mcfg = MCFG_METHOD_13;
		else if((val32 == 0x38000000) || (val32 == 0xB8000000))
			priv->mcfg = MCFG_METHOD_12;
		else if(val32 == 0x30000000)
			priv->mcfg = MCFG_METHOD_11;
		else if(val32 == 0x18000000)
			priv->mcfg = MCFG_METHOD_5;
		else if(val32 == 0x10000000)
			priv->mcfg = MCFG_METHOD_4;
		else if(val32 == 0x04000000)
			priv->mcfg = MCFG_METHOD_3;
		else if(val32 == 0x00800000)
			priv->mcfg = MCFG_METHOD_2;
		else if(val32 == 0x00000000)
			priv->mcfg = MCFG_METHOD_1;
		else
			priv->mcfg = MCFG_METHOD_1;
	}

	{
		unsigned char val8 = (unsigned char)(RTL8169_READ_GMII_REG(ioaddr,3)&0x000f);
		if( val8 == 0x00 ){
			priv->pcfg = PCFG_METHOD_1;
		}
		else if( val8 == 0x01 ){
			priv->pcfg = PCFG_METHOD_2;
		}
		else if( val8 == 0x02 ){
			priv->pcfg = PCFG_METHOD_3;
		}
		else{
			priv->pcfg = PCFG_METHOD_3;
		}
	}

	for (i = ARRAY_SIZE (rtl_chip_info) - 1; i >= 0; i--){
		if (priv->mcfg == rtl_chip_info[i].mcfg) {
			priv->chipset = i;
			goto match;
		}
	}

	//if unknown chip, assume array element #0, original RTL-8169 in this case
	priv->chipset = 0;

match:

	// peculiar place to do PM config. 
	// just copied linux code
	RTL_W8(Cfg9346, Cfg9346_Unlock);
	RTL_W8(Config1, RTL_R8(Config1) | PMEnable);
	RTL_W8(Config5, RTL_R8(Config5) & PMEStatus);
	RTL_W8(Cfg9346, Cfg9346_Lock);

	return 0;
}

//show some information after the driver is inserted
void r1000_show_some_driver_info (
				  PMP_ADAPTER Adapter 
	)
{
	ULONG_PTR ioaddr = (ULONG)Adapter->PortOffset ;

	if(( priv->mcfg == MCFG_METHOD_11 )||
		( priv->mcfg == MCFG_METHOD_12 )||
		( priv->mcfg == MCFG_METHOD_16 ))	{

			DBGPRINT( MP_LOUD, \
				("Realtek RTL8168/8111 Family PCI-E Gigabit Ethernet Network Adapter\n"));
	}

	else if((priv->mcfg==MCFG_METHOD_13)||(priv->mcfg==MCFG_METHOD_14)||(priv->mcfg==MCFG_METHOD_15)) {
		DBGPRINT( MP_LOUD, \
			("Realtek RTL8139/810x Family Fast Ethernet Network Adapter\n"));
	}
	else {
		DBGPRINT( MP_LOUD, \
			("Realtek RTL8169/8110 Family Gigabit Ethernet Network Adapter\n"));
	}

	if(RTL_R8(PHYstatus) & LinkStatus){
		DBGPRINT( MP_LOUD, \
			("Link Status:%s\n","Linked"));

		if(RTL_R8(PHYstatus) & _1000Mbps) {
			DBGPRINT( MP_LOUD, \
				("Link Speed:1000Mbps\n"));
		}

		else if(RTL_R8(PHYstatus) & _100Mbps) {
			DBGPRINT( MP_LOUD, \
				("Link Speed:100Mbps\n"));
		}
		else if(RTL_R8(PHYstatus) & _10Mbps) {
			DBGPRINT( MP_LOUD,\
				("Link Speed:10Mbps\n"));
		}
		DBGPRINT( MP_LOUD, \
			("Duplex mode:%s\n",RTL_R8(PHYstatus)&FullDup?"Full-Duplex":"Half-Duplex"));

	}else{

		DBGPRINT( MP_LOUD, \
			("Link Status:%s\n","Not Linked"));
	}
}


// assumption :
// for this function and most in this module..
// NdisMRegisterIoPortRange(.,,,.) MUST have been called prior and
// adapter->portoffset VALID, != NULL
//

//At this point...
// state of the NIC HW :: SW reset done and INTs disabled. Rx & Tx units disabled
// realtek RBD & TBD base address registers loaded w/ meaningful values
int r1000_init_one ( PMP_ADAPTER Adapter )
{
	static int board_idx = -1;
	int i=0, speed_opt = SPEED_100;
	int duplex_opt = DUPLEX_FULL ;
	int autoneg_opt = AUTONEG_ENABLE;

	ULONG_PTR ioaddr = Adapter->PortOffset;

	board_idx++;

	// find chip variants
	r1000_find_config_method ( Adapter );

	ASSERT (ioaddr != 0);

	DBGPRINT(	MP_LOUD, \
				( \
					"Identified chip type is '%s'.\n",\
					rtl_chip_info[priv->chipset].name) \
				) \
	
	//  WARNING !!
	/***	Chip RTL8169s is very sensitive to the following order of configurations.
	***		Indirectly some timing issues maybe. If you change, things dont work, mainly
	***		because this chip doesn't seem to retain the Tx and Rx config values at
	***		device registers 0x40 & 0x44, what we write into.		
	**		Other chips like the one i tested, 8111 are robust and are not so
	**		sensitive to this order or timing			****/

	// Config MAC layer
	i = r1000_hw_MAC_config ( Adapter ) ;
	if ( i == -1 )
		return -1 ;

	// Config PHY
	r1000_hw_PHY_config( Adapter );
	
	// set speed and duples mode
	i = r1000_set_medium( (USHORT)speed_opt,(UCHAR)duplex_opt,(UCHAR)autoneg_opt, Adapter );
	if ( i == -1 )
		return -1 ;

	//show some information after the driver is inserted
	r1000_show_some_driver_info ( Adapter ) ;
	
	return 0 ;
}

//RESET PHY layer
VOID ResetPhy(
    IN PMP_ADAPTER A
    )
{
	int val, phy_reset_expiretime = 50;
	ULONG_PTR ioaddr = A->PortOffset ;

	DBGPRINT( MP_INFO, \
			("Reset Realtek1G PHY\n"));

	val = ( RTL8169_READ_GMII_REG( ioaddr, 0 ) | 0x8000 ) & 0xffff;
	
	RTL8169_WRITE_GMII_REG( ioaddr, 0, val );

	do //waiting for phy reset
	{
		if( RTL8169_READ_GMII_REG( ioaddr, 0 ) & 0x8000 ){
			phy_reset_expiretime --;
			udelay(100);
		}
		else{
			break;
		}
	}while( phy_reset_expiretime >= 0 );

	ASSERT( phy_reset_expiretime > 0 );
}

