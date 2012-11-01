
#ifndef	_linux_h_
#define	_linux_h_

/****	//new additions for realtek driver		*******/
#define		u32		ULONG
#define		u16		USHORT
#define		u8		UCHAR


#define			readb(x)			READ_PORT_UCHAR((PUCHAR)(x))
#define			readw(x)			READ_PORT_USHORT((PUSHORT)(x))
#define			readl(x)			READ_PORT_ULONG((PULONG)(x))


#define		writeb(val,port)		WRITE_PORT_UCHAR((PUCHAR)(port), val)
#define		writew(val,port)		WRITE_PORT_USHORT((PUSHORT)(port), val)

#define		udelay(i)	NdisStallExecution(i)

#define AUTONEG_DISABLE         0x00
#define AUTONEG_ENABLE          0x01

/* The forced speed, 10Mb, 100Mb, gigabit, 2.5Gb, 10GbE. */
#define SPEED_10                10
#define SPEED_100               100
#define SPEED_1000              1000
#define SPEED_2500              2500
#define SPEED_10000             10000
 
/* Duplex, half or full. */
#define DUPLEX_HALF             0x00
#define DUPLEX_FULL             0x01


#define RX_FIFO_THRESH      7       /* 7 means NO threshold, Rx buffer level before first PCI xfer.  */
#define RX_DMA_BURST        7       /* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST        7       /* Maximum PCI burst, '6' is 1024 */
#define ETTh                0x3F    /* 0x3F means NO threshold */

/***************	new additions for realtek driver END		***********************/




#undef RTL8169_JUMBO_FRAME_SUPPORT
#undef RTL8169_DEBUG

//#define RTL8169_HW_FLOW_CONTROL_SUPPORT
#define RTL8169_DEBUG	1


#define MCFG_METHOD_1		0x01
#define MCFG_METHOD_2		0x02
#define MCFG_METHOD_3		0x03
#define MCFG_METHOD_4		0x04
#define MCFG_METHOD_5		0x05
#define MCFG_METHOD_11		0x0B
#define MCFG_METHOD_12		0x0C
#define MCFG_METHOD_13		0x0D
#define MCFG_METHOD_14		0x0E
#define MCFG_METHOD_15		0x0F
#define MCFG_METHOD_16		0x10

#define PCFG_METHOD_1		0x01	//PHY Reg 0x03 bit0-3 == 0x0000
#define PCFG_METHOD_2		0x02	//PHY Reg 0x03 bit0-3 == 0x0001
#define PCFG_METHOD_3		0x03	//PHY Reg 0x03 bit0-3 == 0x0002

struct r1000_private {

	int chipset;
	int mcfg;
	int pcfg;

} r1000_private ;


#define InterFrameGap	0x03		/* 3 means InterFrameGap = the shortest one */


/* write/read MMIO register */
#define RTL_W8(reg, val8)	writeb ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)	writew ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)	WRITE_PORT_ULONG((PULONG)(ioaddr + reg),(ULONG)val32 )
#define RTL_R8(reg)			readb (ioaddr + (reg))
#define RTL_R16(reg)		readw (ioaddr + (reg))
#define RTL_R32(reg)		READ_PORT_ULONG((PULONG)(ioaddr + reg))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))


const static struct {
	const char *name;
	u8 mcfg;		 /* depend on documents of Realtek */
	u32 RxConfigMask; 	/* should clear the bits supported by this chip */
} rtl_chip_info[] = {
	{ "RTL8169",  MCFG_METHOD_1,  0xff7e1880 },
	{ "RTL8169S/8110S",  MCFG_METHOD_2,  0xff7e1880 },
	{ "RTL8169S/8110S",  MCFG_METHOD_3,  0xff7e1880 },
	{ "RTL8169SB/8110SB",  MCFG_METHOD_4,  0xff7e1880 },
	{ "RTL8169SC/8110SC",  MCFG_METHOD_5,  0xff7e1880 },
	{ "RTL8168B/8111B",  MCFG_METHOD_11,  0xff7e1880 },
	{ "RTL8168B/8111B",  MCFG_METHOD_12,  0xff7e1880 },
	{ "RTL8101E",  MCFG_METHOD_13,  0xff7e1880 },
	{ "RTL8100E",  MCFG_METHOD_14,  0xff7e1880 },
	{ "RTL8100E",  MCFG_METHOD_15,  0xff7e1880 },
	{ "RTL8168C",  MCFG_METHOD_16,  0xff7e1880 },
	{ 0 }
};


enum RTL8169_registers {
	/* PHY access */
	PHYAR_Flag = 0x80000000,
	PHYAR_Write = 0x80000000,
	PHYAR_Read = 0x00000000,
	PHYAR_Reg_Mask = 0x1f,
	PHYAR_Reg_shift = 16,
	PHYAR_Data_Mask = 0xffff,

	MAC0 = 0x0,
	MAR0 = 0x8,
	DumpTally = 0x10,  // 64 bit register	TxDescStartAddr	= 0x20,
	TxDescStartAddr	= 0x20,
	TxHDescStartAddr= 0x28,
	FLASH	= 0x30,
	ERSR	= 0x36,
	ChipCmd	= 0x37,
	TxPoll	= 0x38,
	IntrMask = 0x3C,
	IntrStatus = 0x3E,
	TxConfig = 0x40,
	RxConfig = 0x44,
	RxMissed = 0x4C,
	Cfg9346 = 0x50,
	Config0	= 0x51,
	Config1	= 0x52,
	Config2	= 0x53,
	Config3	= 0x54,
	Config4	= 0x55,
	Config5	= 0x56,
	MultiIntr = 0x5C,
	PHYAR	= 0x60,
	TBICSR	= 0x64,
	TBI_ANAR = 0x68,
	TBI_LPAR = 0x6A,
	PHYstatus = 0x6C,
	RxMaxSize = 0xDA,
	CPlusCmd = 0xE0,
	DBG_reg = 0xD1,
	RxDescStartAddr	= 0xE4,
	ETThReg	= 0xEC,
	FuncEvent	= 0xF0,
	FuncEventMask	= 0xF4,
	FuncPresetState	= 0xF8,
	FuncForceEvent	= 0xFC,		
};

enum RTL8169_register_content {
	/*InterruptStatusBits*/

	PMEnable	= (1 << 0),	/* Power Management Enable */
	PMEStatus	= (1 << 0),	/* PME status can be reset by PCI RST# */

	SYSErr 		= 0x8000,
	PCSTimeout	= 0x4000,
	SWInt		= 0x0100,
	TxDescUnavail	= 0x80,
	RxFIFOOver 	= 0x40,
	LinkChg 	= 0x20,
	RxOverflow 	= 0x10,
	TxErr 	= 0x08,
	TxOK 	= 0x04,
	RxErr 	= 0x02,
	RxOK 	= 0x01,

	/*RxStatusDesc*/
	RxRES = 0x00200000,
	RxCRC = 0x00080000,
	RxRUNT= 0x00100000,
	RxRWT = 0x00400000,

	/*ChipCmdBits*/
	CmdReset = 0x10,
	CmdRxEnb = 0x08,
	CmdTxEnb = 0x04,
	RxBufEmpty = 0x01,

	/*Cfg9346Bits*/
	Cfg9346_Lock = 0x00,
	Cfg9346_Unlock = 0xC0,

	/*rx_mode_bits*/
	AcceptErr = 0x20,
	AcceptRunt = 0x10,
	AcceptBroadcast = 0x08,
	AcceptMulticast = 0x04,
	AcceptMyPhys = 0x02,
	AcceptAllPhys = 0x01,

	/*RxConfigBits*/
	RxCfgFIFOShift = 13,
	RxCfgDMAShift = 8,

	/*TxConfigBits*/
	TxInterFrameGapShift = 24,
	TxDMAShift = 8,		/* DMA burst value (0-7) is shift this many bits */


	/*GIGABIT_PHY_registers*/
	PHY_CTRL_REG = 0,
	PHY_STAT_REG = 1,
	PHY_AUTO_NEGO_REG = 4,
	PHY_1000_CTRL_REG = 9,
	PHY_GBSR_REG = 0xa,
	PHY_GBESR_REG = 0xf,

	/*GIGABIT_PHY_REG_BIT*/
	PHY_Restart_Auto_Nego	= 0x0200,
	PHY_Enable_Auto_Nego	= 0x1000,

	//PHY_STAT_REG = 1;
	PHY_Auto_Neco_Comp	= 0x0020,

	//PHY_AUTO_NEGO_REG = 4;
	PHY_Cap_10_Half		= 0x0020,
	PHY_Cap_10_Full		= 0x0040,
	PHY_Cap_100_Half	= 0x0080,
	PHY_Cap_100_Full	= 0x0100,

	PHY_Cap_PAUSE           = 0x0400,
	PHY_Cap_ASYM_PAUSE      = 0x0800,

	/*rtl8169_PHYstatus (MAC offset 0x6C)*/
	TBI_Enable	= 0x80,
	TxFlowCtrl	= 0x40,
	RxFlowCtrl	= 0x20,
	_1000Mbps	= 0x10,
	_100Mbps	= 0x08,
	_10Mbps		= 0x04,
	LinkStatus	= 0x02,
	FullDup		= 0x01,


	//PHY_1000_CTRL_REG = 9;
	PHY_Cap_1000_Full	= 0x0200,
	PHY_Cap_1000_Half	= 0x0100,

	PHY_Cap_Null		= 0x0,


	/*_MediaType*/
	_10_Half	= 0x01,
	_10_Full	= 0x02,
	_100_Half	= 0x04,
	_100_Full	= 0x08,
	_1000_Full	= 0x10,

	/*_TBICSRBit*/
	TBILinkOK 	= 0x02000000,
};

enum _DescStatusBit {
	OWNbit	= 0x80000000,
	EORbit	= 0x40000000,
	FSbit	= 0x2000,
	LSbit	= 0x1000,
	MARbit	= 0x08000000,
	PAMbit	= 0x04000000,
	BARbit	= 0x02000000
};


static const u16 r1000_intr_mask = LinkChg | RxFIFOOver | TxErr | TxOK | RxErr | RxOK ;


#endif

