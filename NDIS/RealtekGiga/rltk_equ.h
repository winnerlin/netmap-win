
#ifndef _RLTK_EQU_H
#define _RLTK_EQU_H


//-------------------------------------------------------------------------
// Ethernet Frame Sizes
//-------------------------------------------------------------------------
#define ETHERNET_ADDRESS_LENGTH         6
#define ETHERNET_HEADER_SIZE            14
#define MINIMUM_ETHERNET_PACKET_SIZE    60
#define MAXIMUM_ETHERNET_PACKET_SIZE    1514

#define MAX_MULTICAST_ADDRESSES         32
#define TBD_BUFFER_SIZE                 0XE0 // 224
#define COALESCE_BUFFER_SIZE            2048
#define ETH_MAX_COPY_LENGTH             0x80 // 128

//-------------------------------------------------------------------------
// Ndis/Adapter driver constants
//-------------------------------------------------------------------------
#define MAX_PHYS_DESC                   16
#define MAX_RECEIVE_DESCRIPTORS         1024 // 0x400
#define NUM_RMD                         10


//--------------------------------------------------------------------------
//    Equates Added for NDIS 4
//--------------------------------------------------------------------------
#define  NUM_BYTES_PROTOCOL_RESERVED_SECTION    16
#define  MAX_NUM_ALLOCATED_RECV_BUFFERS                 64
#define  MIN_NUM_RECV_BUFFER                            4
#define  MAX_ARRAY_SEND_PACKETS                 8
// limit our receive routine to indicating this many at a time
#define  MAX_ARRAY_RECEIVE_PACKETS              16
#define  MAC_RESERVED_SWRECV_BUFFERPTR                  0
#define  MAX_PACKETS_TO_ADD                     32

//-------------------------------------------------------------------------
//- Miscellaneous Equates
//-------------------------------------------------------------------------
#define CR      0x0D        // Carriage Return
#define LF      0x0A        // Line Feed

#ifndef FALSE
#define FALSE       0
#define TRUE        1
#endif

//-------------------------------------------------------------------------
// Bit Mask definitions
//-------------------------------------------------------------------------
#define BIT_0       0x0001
#define BIT_1       0x0002
#define BIT_2       0x0004
#define BIT_3       0x0008
#define BIT_4       0x0010
#define BIT_5       0x0020
#define BIT_6       0x0040
#define BIT_7       0x0080
#define BIT_8       0x0100
#define BIT_9       0x0200
#define BIT_10      0x0400
#define BIT_11      0x0800
#define BIT_12      0x1000
#define BIT_13      0x2000
#define BIT_14      0x4000
#define BIT_15      0x8000
#define BIT_24      0x01000000
#define BIT_28      0x10000000

#define BIT_0_2     0x0007
#define BIT_0_3     0x000F
#define BIT_0_4     0x001F
#define BIT_0_5     0x003F
#define BIT_0_6     0x007F
#define BIT_0_7     0x00FF
#define BIT_0_8     0x01FF
#define BIT_0_13    0x3FFF
#define BIT_0_12    0x1FFF
#define BIT_0_15    0xFFFF
#define BIT_1_2     0x0006
#define BIT_1_3     0x000E
#define BIT_2_5     0x003C
#define BIT_3_4     0x0018
#define BIT_4_5     0x0030
#define BIT_4_6     0x0070
#define BIT_4_7     0x00F0
#define BIT_5_7     0x00E0
#define BIT_5_9     0x03E0
#define BIT_5_12    0x1FE0
#define BIT_5_15    0xFFE0
#define BIT_6_7     0x00c0
#define BIT_7_11    0x0F80
#define BIT_8_10    0x0700
#define BIT_9_13    0x3E00
#define BIT_12_15   0xF000
#define BIT_8_15    0xFF00 

#define BIT_16_20   0x001F0000
#define BIT_21_25   0x03E00000
#define BIT_26_27   0x0C000000

// in order to make our custom oids hopefully somewhat unique
// we will use 0xFF (indicating implementation specific OID)
//               XX (the custom OID number - providing 255 possible custom oids)
#define OID_CUSTOM_DRIVER_SET       0xFFA0C901
#define OID_CUSTOM_DRIVER_QUERY     0xFFA0C902
#define OID_CUSTOM_ARRAY            0xFFA0C903
#define OID_CUSTOM_STRING           0xFFA0C904
#define OID_CUSTOM_METHOD           0xFFA0C905

#define CMD_BUS_MASTER              BIT_2

#endif  // _RLTK_EQU_H

