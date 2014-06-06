/** The main API for the program. */

#ifndef _CPL_DEFS_H_
#define _CPL_DEFS_H_

#include <tchar.h>
#include <string>
#include <Windows.h>	// we need to include windows to include ftd2xx.h

#define	BARST_VERSION		21000	// it's 2.10.00

#define MIN_PIPE_BUF_SIZE	20	// smallest named pipe buffer possible
#define	DEVICE_NAME_SIZE	8	// size of the unique internal name given for all devices/managers
#define MIN_BUFF_IN			256	// smallest server read (clients write) buffer for named pipe communicator
#define MIN_BUFF_OUT		256	// smallest server write (clients read) buffer for named pipe communicator
#define MIN_FTDI_LIB_VER	0x00030204	// the lowest FTDI version we accept for FTDI lib driver
#define MIN_RTV_LIB_VER		1080	// the lowest RTV version we accept for RTV dll lib driver
#define FTDI_MAX_BUFF_H		(510*128)	// max buffer that FTDI can r/w on high speed devices.
#define FTDI_MAX_BUFF_L		(62*1024) // max buffer that FTDI can r/w on low speed devices.
#define FTDI_BAUD_2232H		200000	// clock rate is actually 1MHz = 200000*5
#define FTDI_BAUD_DEFAULT	62500	// clock rate is actually 1MHz = 62500*16
#define SERIAL_MAX_LENGTH	24		// maximum length serial comports can be (e.g. com4)


#define	BAD_INPUT_PARAMS	1
#define	NO_SYS_RESOURCE		2	// out of memory, etc.
#define	ALREADY_OPEN		3	// tried to open device etc. that was already opened. or made request already pending
#define	SIZE_MISSMATCH		4	// size of massage doesn't match expected size.
#define	INVALID_CHANN		5	// channel requested is not set, out of range etc.
#define UNKWN_ERROR			6
#define	DRIVER_ERROR		7
#define	DEVICE_CLOSING		8	// device is closing so no data can be read/sent
#define	INVALID_DEVICE		9	// the device you tried to create (e.g. ADC) isn't recognized
#define	INACTIVE_DEVICE		10	// tried to do something with inactive device
#define	INVALID_COMMAND		11	// didn't understand what you wanted, or command is invalid in this state/stage
#define	UNEXPECTED_READ		12	// when the packet read doesn't match anything
#define	NO_CHAN				13	// channel wasn't created as requested because something went wrong. Or no channel was provided
#define	BUFF_TOO_SMALL		14	// provided buffer was too small, or wanted to write more than the buffer can take
#define	NOT_FOUND			15	// library or object wasn't found
#define	TIMED_OUT			16	// timed out while waiting for something
#define	INVALID_MAN			17	// the manager (e.g. rtv, ftdi) you wanted to communicate with is not recognized
#define	RW_FAILED			18
#define LIBRARY_ERROR		19

/** The error codes provided by us range from 1-100. Windows, the RTV and FTDI dlls also provide their own error code
	ranges. These macros convert those error codes so they are within the range noted next to them. */
#define FT_ERROR(x, nTemp) (((nTemp= x)!=0)?(nTemp + 100):0)	// 101 + 1-19 = (101 to 200)
#define RTV_ERROR(x, nTemp) (((nTemp= -x)!=0)?(nTemp + 200):0)	// 201 + 1-201 = (201 to 500)
#define MCDAQ_ERROR(x, nTemp) (((nTemp= x)!=0)?(nTemp + 500):0)	// 101 + 1-19 = (501 to 1601)
#define WIN_ERROR(x, nTemp) (((nTemp= x)!=0)?(nTemp + 10000):0)	// 10001 + ? = (10001 to ?)


/** Provides a general way to compile this project in unicode or multi-byte. Currently only
	multi-byte compilation has been tested (i.e. _UNICODE isn't defined). */
#ifdef _UNICODE
#define tstring wstring
#define tostringstream wostringstream
#define tstringstream wstringstream
#else
#define tstring string
#define tostringstream ostringstream
#define tstringstream stringstream
#endif


/** Defines constants used throught the project. */
enum EQueryType
{
	/*** Everything sent to Barst starts with SBaseIn, optionally followed by other structs (each of which is prefaced
	by SBase). Response by Barst is also SBaseIn or SBaseOut, optionally followed by other structs (each of which is prefaced
	by SBase). The eType parameter of these structs are defined in this enum and determines the reason/type of the clients/server's
	communication. The response eType of SBaseIn is typically the same eType as the eType sent, except in case of error it might be
	eResponse or anything else, or if the response is a SBaseOut eType would be on of the eResponseExX. 
	
	If data is sent to multiple layers, the lowest layer responds, and each layer needs
	its own SBaseIn header. For instance, if you send data to the main manager which then passes it on to a manager
	you have to provide two SBaseIn struct for each manager. The response would then come from the manager. ***/

	/** Cat A - Indicates SBaseIn struct. */
	eNone= 0,	// invalid
	eQuery,		// Requests info, response is typically eResponseEx with other stuff following
	eSet,		// Sets something, typically sends target specific info following SBaseIn
	eDelete,	// Deletes a channel or manager
	ePassOn,	// Tells upper layer to passs data on to next layer
	eData,		// SBaseIn followed by SBase and data, or just SBaseIn if that's the data
	eTrigger,	// Used to make a device do something
	eResponse,	// General response
	eVersion,	// dwInfo parameter is the version #
	eActivate,	// Tells server to activate a device
	eInactivate,	// Tells server to de-activate a device
	/*** Cat B - SBaseOut ***/
	eResponseEx,	// szName
	eResponseExL,	// llLargeInteger
	eResponseExD,	// dDouble
	/*** Cat C ***/
	// ftdi struct info
	eFTDIChan,		// SBase followed by FT_DEVICE_LIST_INFO_NODE
	eFTDIChanInit,	// SBase followed by SChanInitFTDI
	eFTDIPeriphInit,	// SBase followed by SInitPeriphFT
	eFTDIMultiWriteInit,	// SBase followed by SValveInit
	eFTDIADCInit,	// SBase followed by SADCInit
	eFTDIMultiReadInit,	// SBase followed by SValveInit
	eFTDIPinReadInit,	// SBase followed by SPinInit
	eFTDIPinWriteInit,	// SBase followed by SPinInit
	eRTVChanInit,		// SBase followed by SChanInitRTV
	eSerialChanInit,	//  SBase followed by SChanInitSerial
	// possible buffer types
	eFTDIMultiWriteData,	// SBase followed by array of multi write data in the form of SValveData
	eADCData,					// SADCData struct (only from sBase ahead, sDataBase has its own eType)
	eFTDIMultiReadData,			// SBase followed by multi read pin data in form of bool array
	eFTDIPinWDataArray,			// SBase followed by pin data in form of SPinWData array
	eFTDIPinWDataBufArray,		// SBase followed by a SPinWData followed by pin data in form of unsigned char array
	eFTDIPinRDataArray,			// SBase followed by pin data read in form of unsigned char array
	eRTVImageBuf,				// SBase followed by a RTV image buffer.
	eSerialWriteData,			// SBase followed by SSerialData followed by data to be written
	eSerialReadData,			/// SBase followed by SSerialData
	eMCDAQChanInit,		// SBase followed by SChanInitMCDAQ
	eMCDAQWriteData,	// SBase followed by SMCDAQWData
	eCancelReadRequest,	// SBaseIn indicating that a previous read request by the pipe should be canceld
	eServerTime,		// SBaseIn followed by SBase followed by SPerfTime


	// managers - the values that eType2 can take
	eFTDIMan= 1000,
	eRTVMan,
	eSerialMan,
	eMCDAQMan,
};




/** The folloiwng 3 structs are used as prefaces to messages. See API for full details. */

// Simplest struct that can be sent on its own from/to user.
typedef struct SBaseIn
{
	DWORD		dwSize;	// the size of the SBaseIn struct (16)
	EQueryType	eType;	// the reason this was sent
	// one knows which of the three types was used based on eType
	union {
	int			nChan;	// the channel number
	EQueryType	eType2;	// a secondary reason for sending
	DWORD		dwInfo;	// When a DWORD is required
	};
	int			nError;	// an error code.
} SBaseIn;

// struct used to preface other structs.
typedef struct SBase
{
	DWORD		dwSize;	// the size of SBase + the size of the struct following
	EQueryType	eType;	// the struct following
} SBase;

// Alternative to SBaseIn so we can send a it more info. Typically used when we send info on a channel.
typedef struct SBaseOut
{
	// SBaseIn with sBaseIn.eType one of the eResponseExX types. sBaseIn.dwSize is 32 bytes.
	SBaseIn		sBaseIn;
	union {
	TCHAR		szName[DEVICE_NAME_SIZE];	// eResponseEx
	double		dDouble;					// eResponseExD
	LARGE_INTEGER	llLargeInteger;			// eResponseExL
	};
	bool		bActive;					// if the channels this was sent for is active.
} SBaseOut;




typedef struct SPerfTime
{
	double		dRelativeTime;
	double		dUTCTime;
} SPerfTime;




/******************** FTDI ************************/

/** This struct is used to initialize a FTDI channel. Although the user supplies this struct
	when initializing the channel, we make sure it's large enough for required data flow.
	But if the user supplied value is larger, we use the user supplied value, even if it's
	enourmously large, so don't do it. You can leave all set to zero.*/
typedef struct SChanInitFTDI
{
	// sizes must be larger than what we'll ever write/read
	DWORD		dwBuffIn;	// Size of the max buffer that client writes to server
	DWORD		dwBuffOut;	// Size of the max buffer that client reads from server
	DWORD		dwBaud;		// The baud rate used by the channel. We disregard the value supplied by the user.
} SChanInitFTDI;


/** Params used to initialize an FTDI device (a channel can have multiple devices). User doesn't supply
	this info, we just make this availible upon user query request for debugging. */
typedef struct SInitPeriphFT
{
	int				nChan;	// the channel number of this device within the FTDI channel.
	// Holds the max buffer length that the write buffer will EVER be.
	// The actual length of buffer written can vary depending on which devices are active
	// but you can safley write to buffer for this length. For instance, if multi write and adc
	// devices are active, say multi write needs only 18 bytes and adc needs 4000 bytes to be written. Then 4000
	// bytes will be written, but the multi write device will have to initialize the full 4000 bytes, not just the 18 bytes
	// with the default values. This is typically done when inactivating, the state in which each device starts.
	// This size is the max of all devices, including padding if needed.
	DWORD			dwBuff;
	// min buffer THIS device needs, actual buffer written/read might be larger (dwBuff) but not smaller
	// while this device is active we are guaranteed to write/read dwMinSizeR/W
	DWORD			dwMinSizeR;
	DWORD			dwMinSizeW;
	DWORD			dwMaxBaud;	// the baud rate this device needs. We use the min of all devices.
	// the bit output mode (FT_xx), sync/async bit bang. If both are availible for all devices we use async.
	// We and all the bit modes of all the devices so we find mode supported by all devices.
	unsigned char	ucBitMode;
	unsigned char	ucBitOutput;	// the bits of the USB bus that are outputs for this device.
} SInitPeriphFT;



/** Params used to initialize the read/write shift register devices. These are the 
	Serial in paralell out or paralell in serial out device, which write and read
	paralell bytes respectivly. Such as the 74HC595 or 74HC165 device, respectivly. 
	The eType variable used	in the prefacing SBase determines if it's a input or 
	output device. */

typedef struct SValveInit
{
	DWORD			dwBoards;	// the number of boards connected (each has 8 bits).
	// the number of clock cycles to use for each tick (i.e. actual baud rate will be bausrate/dwClkPerData)
	DWORD			dwClkPerData;	
	unsigned char	ucClk;	// the bit number (0-7) that is the clock line
	unsigned char	ucData;	// the bit number (0-7) that is the data line
	unsigned char	ucLatch;	// the bit number (0-7) that is the latch (Sa on MC74HC165A) signal
	// if reading device, and if true than we will continuosly read and send the data read to the
	// client that initially triggered this device. This ensures we read as much data as possible.
	// If it's a writing device we ignore this parameter. 
	bool			bContinuous;
} SValveInit;


/** For _output_ devices created with SValveInit (shift register devices) we use an array of this
	struct to set particular bits to a high/low value. This ensures that we only change the status
	of a particular bit that we want to change while leaving all other bits unchanged. */
typedef struct SValveData
{
	/** the index of the bit to be changed. Each serial board that is attached outputs 8 bits and you
		can have multiple boards daisy chained to each other increasing to unlimited outputs. 
		On each board itself we count the pins or outputs 0-7 from left to write (inverse of how
		a byte is numbered, right to left). So if one board is connected, the physical outputs of 
		the board will be 0,1,2,3,4,5,6,7 in order from left to right. When more than one board is 
		connected, i.e. they are daisy chained then the total number of outputs are n*8 where n is the
		number of boards connected. In this case, we count boards from most distant to computer to the 
		closest to the computer, or assuming the leftmost board is connected to the computer, the 
		rightmost board is board #1 and the leftmost board is board #n. So in the following layout:
		computer->board#n->board#n-1...board#2->board#1 the number of individual pins on each board are:
		computer->(n-1),(n-1)+1,(n-1)+2,(n-1)+3,(n-1)+4,(n-1)+5,(n-1)+6,(n-1)+7...8,9,10,11,12,13,14,15->
		0,1,2,3,4,5,6,7. The indexes given in the last example is the index number to use in usIndex. */
	unsigned short	usIndex;
	bool			bValue;		// true to set bit high, false to set low.
} SValveData;



/** Initialization struct to create an FTDI device which provides direct access
	to the pins on the USB device - like in bit bang mode. You can individually control 
	one or more bits of the 8-bit USB bus both as input or output. The eType variable used
	in the prefacing SBase determines if it's a input or output device. */
typedef struct SPinInit
{
	/** The number of bytes that will be written or read from the bus. For instance,
		you can clock out a given number of bytes with the baud rate of the device
		(which are defined in FTDI_MAX_BUFF_H/L, FTDI_BAUD_2232H/DEFAULT). */
	unsigned short	usBytesUsed;
	/** Which pins are active for this device, either as input or output. 
		The bits set will be the active pins for this device. */
	unsigned char	ucActivePins;
	/** If this is an output device, what the initial value (high/low) will be
		for the active pins. */
	unsigned char	ucInitialVal;
	/** If this is an input device, and if true than we will continuosly read and send the data read to the client
		that initially triggered this device. This ensures we read as much data as possible. If it's a output device
		we ignore this parameter.*/
	bool			bContinuous;
} SPinInit;

/** Used to send data for an _output_ device initialized with the SPinInit struct. You send an
	array of this struct prefaced by an SBase struct. Each element in the array defines the output
	for a part of the buffer output. That is, you can provide exactly what the output will be with
	an array of bytes (see the EQueryType for that) or you can send an array of this struct where, each
	element lets you repeat the same buffer byte multiple times so that if there is a lot of duplicity 
	in the output, you can send a much smaller message than the full output buffer length. */
typedef struct SPinWData
{
	// how many times this byte will be repeated in the output buffer.
	unsigned short	usRepeat;
	// the the byte to output usRepeat times.
	unsigned char	ucValue;
	/*	Which of the output bytes to update. This lets you update only some of the output bytes
		while not changing the others. For instance, if the pins for this device were selected as bits 
		3 and 7, i.e. ucActivePins was set to 0x88. But you only want to update bit 3 with this struct,
		then you set ucPinSelect to 0x08 and only bit 3 will be changed. The other bit will remain as is.*/
	unsigned char	ucPinSelect;
} SPinWData;



/** Initialization struct to create an FTDI device which provides access to
	the CPL ADC device. */
typedef struct SADCInit
{
	/** When communicating with the ADC device we continueously read/write to it. 
		The larger the buffer we write/read at once the faster it preforms. For instance,
		for fastest communication we would write in buffer mutliples of FTDI_MAX_BUFF_H/L.
		Because each USB bus can be used for multiple devices, if we were to write in multiples
		of FTDI_MAX_BUFF_H/L then although it'd be most efficient for the ADC device, during this
		write/read other devices of this bus will have to wait until we are finished writing 
		this buffer and we want to write again to be able to update the output buffer to write to 
		their device. This means, the larger the buffer the more we have to wait between writes to 
		other devices. Although each write/read from a device gets its own timestamp when it occured
		so that we still know when that occured, the write occur with less frequency. 
		fUSBBuffToUse tells us the percentage (0-100) of FTDI_MAX_BUFF_H/L to use for ADC r/w. 
		The smaller this is, the faster other devices will be able to update, but might reduce
		the ADC bit rate. */
	float			fUSBBuffToUse;
	/** This parameter gives you control over how often the ADC sends the data read to the device that
		triggered it. The device will wait until dwDataPerTrans data points for each channel (if the
		second channel is active) has been accumilated and than send eactly dwDataPerTrans (for each channel) 
		data points	to the client. */
	DWORD			dwDataPerTrans;
	/** defines which pin in the USB bus is the clock pin to the ADC. */
	unsigned char	ucClk;
	/** Defines which pins on the USB bus are data pins. The data pins always start from pin 7 and go
		until an even number - e.g. 6 (data is pins 6, and 7), or 4 (data is pins 4, 5, 6, and 7).
		Currently, in Alder board this must be 4. */
	unsigned char	ucLowestDataBit;
	/** Indicates the number of bits connected to the ADC data port. Range is [0, 6]
		0 indicates 2 bits are connected, while 6 indiactes the full port, 8 bits are connected. */
	unsigned char	ucDataBits;
	/** The adc samplig rate in Hz is MCLK/(ucRateFilter*A + B)
		Where MCLK is the crytal resonator frequency (6MHz for the ADC Board).
		If bChop is true, A is 128, ucRateFilter can range between 2 to 127 inclusive, and B is 249 if bChan2 and bChan1 are true, otherwise it's 248.
		If bChop is false, A is 64, ucRateFilter can range between 3 to 127 inclusive, and B is 207 if bChan2 and bChan1 are true, otherwise it's 206. */
	unsigned char	ucRateFilter;
	/** Whether chopping mode (noise reduction is active). */
	bool			bChop;
	/** If channel 1 in the ADC device is read. */
	bool			bChan1;
	/** If channel 2 in the ADC device is read. */
	bool			bChan2;
	/** The voltage input range of the active channels. 
		0: +/- 10V.
		1: 0 - 10V.
		2: +/- 5V.
		3: 0 - 5V.		*/
	unsigned char	ucInputRange;
	/** The bit depth of the ADC data read. It's either 16 or 24. */
	unsigned char	ucBitsPerData;
	/** True if we should read the status register from the ADC device. The status register 
		helps us determine if errors occured. If bChan2 is true, this also MUST be true. 
		This should be true. */
	bool			bStatusReg; 
	/** Sets how the ADC is connected to the USB bus. Currently, in Alder board this must be true.
		This indicates that the data pins on the USB bus are flipped relative to the ADC pins. I.e.
		pin 7 connects to pin 0 etc. */
	bool			bReverseBytes;
	/** Whether we configure the ADC before reading. */
	bool			bConfigureADC;
} SADCInit;


/** The struct that the ADC device sends to the client filled with DWORD data read from the ADC. 
	The DWORD data immidiatly follows this struct. */
typedef struct SADCData
{
	/** According to the rules, each communication starts with an SBaseIn/Out and is possibly followed by 
		an SBase and other structs. In ordeer to be able to send ADC more easily all these structs are included 
		at once in this SADCData struct. sDataBase is the SBaseIn, sBase is the SBase struct and the stuff following
		is considered as the struct following the SBase struct.
		
		For this struct, sDataBase.nError is not an error code, instead, the lower 16 bits indicate how many times
		the data the ADC sent to the USB bus was bad. The upper 16 bits in nError indicates how many times the 
		ADC device overflowed, i.e. how many times the USB bus was too slow to collect the data from the ADC device
		and the ADC device therefore overwrote data. 
		
		eType should be eTrigger. */
	SBaseIn		sDataBase;
	/** sBase.eType is eADCData and sBase.dwSize is sizeof(SADCData)-sizeof(SBaseIn). */
	SBase		sBase;
	/** For each SADCData struct we send to the client, dwPos is incremented by one. This allows to keep track
		if user lost a packet. */
	DWORD		dwPos;
	/** Everytime that we send this struct we always send this many bytes: 
		sizeof(SADCData)+sizeof(DWORD)*SADCInit.dwDataPerTrans*(SADCInit.bChan2?2:1),
		that is because we are expected to send dwDataPerTrans data points per channel for each
		transmission we always send the same amount of data. However sometimes, the last packet might not have 
		enough data to fill up the full dwDataPerTrans data points. Consequently, even though we still send 
		the same number of bytes, dwCount1 and dwCount2 tell you how much actual data points is in this data buffer. 
		
		Immidiatly following the SADCData struct follows channel 1 data, the total buffer length for channel 1 is 
		dwDataPerTrans, the first dwCount1 points of which is the actual channel 1 data. dwChan2Start tells you where
		channel 2 data starts. Because channel 2 data follows channel 1 data, dwChan2Start must always be equal to 
		dwDataPerTrans (the max number of data points for channel 1). If channel 2 is active, the buffer length in data
		point units is also dwDataPerTrans. So dwCount2 tells you how many actual data points is in this buffer. */
	DWORD		dwCount1;
	DWORD		dwChan2Start;
	DWORD		dwCount2;
	/** Each USB transection with the ADC device gets time stamped (uncertainty of the time stamp is a few ms, depending on
		the USB communication uncertainty). The time stamp is given in the dStartTime parameter. Because the data in one USB 
		transection can be broken down and sent in multiple packets based on the dwDataPerTrans parameter. The timestamp
		then is associated with a particular data point within the buffer, because that data point would have been the first data
		point read in a new USB transection. dwChan1S tells you the index of this data point for channel 1 and dwChan2S is
		the index for channel 2 (they should be very close to each other). If this packet doesn't have a data point that
		was timestamped, e.g. because this is the second packet of a single USB transection then dStartTime will be zero. */
	DWORD		dwChan1S;
	DWORD		dwChan2S;
	/** As mentioned in fUSBBuffToUse, we can select different size for the buffer length that is written to the USB device
		at once. If it's very small, than most of the buffer read would be full with ADC data. If it's very large, then it 
		should be mostly empty because it's more efficient. This parameter tells you the percentage of the buffer that was
		filled with ADC data read. If it's close to 100, that means you're close to losing data because the buffer might
		be too small or clock too slow to be able to read the data fast enough. So you should increase the buffer size
		or set a smaller smapling rate in the ADC hardware device. */
	float		fSpaceFull;
	/** Tells you the percentage of time from one read to another that the ADC thread spent extracting the ADC data 
		from the buffer. */
	float		fTimeWorked;
	/** Estimates the data rate of the ADC. It should be near the ADC sampling rate. */
	float		fDataRate;
	/** When we read from the ADC status register, we get additional error info. 
		Bit 0 (for channel 1) and/or bit 4 (for channel 2) is set if the input voltage sensed by the ADC is out of
		range, e.g. it's outside of the +/- 20v range if that's the allowable range.
		Bit 2 (for channel 1) and/or bit 6 (for channel 2) is set if the voltage reference on the chip is not
		sensed. This simply means there's a hardware error. */
	unsigned char	ucError;
	/** See dwChan1S description. */
	double		dStartTime;
} SADCData;






/************* RTV ****************/

/** Struct used to initialize an RTV channel for aquiring video frames. */
typedef struct SChanInitRTV
{
	/** Index 0 in AngeloRTV_Set_Image_Config (see RTV-24 manual). */
	unsigned char		ucBrightness;
	/** Index 1 in AngeloRTV_Set_Image_Config. */
	unsigned char		ucHue;
	/** Index 2 in AngeloRTV_Set_Image_Config. */
	unsigned char		ucUSat;
	/** Index 3 in AngeloRTV_Set_Image_Config. */
	unsigned char		ucVSat;
	/** Index 4 in AngeloRTV_Set_Image_Config. */
	unsigned char		ucLumaContrast;
	/** Index 5 in AngeloRTV_Set_Image_Config. */
	unsigned char		ucLumaFilt;
	/** The image format that RTV will return to us. These are the 
		RTV defined values, such as 3 for RGB24 etc. */
	unsigned char		ucColorFmt;
	/** The video format that RTV uses to capture the video. These are RTV
		defined values such as 0 for full NTSC etc. */
	unsigned char		ucVideoFmt;
	/** If this is true, then every frame aquired will be sent to the client. This ensures
		that no frame is missed. However, if the client doesn't read in time, a lot of RAM
		will be used up quickly for the frames that are in RAM and are waiting to be sent.
		If it's false, a frame will only be sent if no other frame is waiting to be sent. So
		when we queue a frame, as long as the client has not read this frame, no other frame
		will be queued for sending. */
	bool				bLossless;
	/** User doesn't supply these parameters - until end, the channels fills this in after 
		it was created and is sent as a reply. Or user can request this. */
	/** The number of bits per pixel that is used for this channel. Could be 24, 32 etc.*/
	unsigned char		ucBpp;
	/** The width of a frame of this channel. */
	int					nWidth;
	/** The height of a frame of this channel. */
	int					nHeight;
	/** The total buffer length in bytes required to store an image of this channel. Typically
		it's nWidth*nHeight*ucBpp. */
	DWORD				dwBuffSize;
} SChanInitRTV;






/************* CommPort****************/

/** Struct used to initialize a Comm port. */
typedef struct SChanInitSerial
{
	TCHAR	szPortName[SERIAL_MAX_LENGTH];	// The name of the port, e.g. COM1
	DWORD	dwMaxStrWrite;					// max size of any data to be written
	DWORD	dwMaxStrRead;					// max size of any data to be read
	DWORD	dwBaudRate;						// baud rate with which to open the channel
	// The number of stop bits to use. Can be one of 0, 1, or 2. 0 means 1 bit,
    // 1, means 1.5 bits, and 2 means 2 bit.
	unsigned char	ucStopBits;
	unsigned char	ucParity;
	// The number of bits in the bytes transmitted and received. Can be between
    // 4 and 8, including 4 and 8.
	unsigned char	ucByteSize;
} SChanInitSerial;

/** Struct used for sending and reading data from server. **/
typedef struct SSerialData
{
	DWORD	dwSize;		// size of data to read/write
	DWORD	dwTimeout;	// 0 means infinite wait
	char	cStop;		// when reading, the character on which to stop
	bool	bStop;		// if cStop is used, when reading
} SSerialData;



// FT_DEVICE_LIST_INFO_NODE_OS is simmalr to FT_DEVICE_LIST_INFO_NODE, 
// except the last item instead of being a FT_HANDLE which is a void*
// so size is either 4 or 8 bytes, is instead always 8 bytes. The _OS struct is
// always used when sending to clients to ensure x64, x86 comaptibility.
typedef struct _ft_device_list_info_node_os {
	ULONG Flags;
	ULONG Type;
	ULONG ID;
	DWORD LocId;
	char SerialNumber[16];
	char Description[64];
	unsigned __int64 ftHandle;
} FT_DEVICE_LIST_INFO_NODE_OS;





/************* MCDAQ device****************/


/** Struct used to initialize a MCDAQ device. */
typedef struct SChanInitMCDAQ
{
	unsigned char	ucDirection;	// 0 for reading, 1 for writing, 2 for read/write
	unsigned short	usInitialVal;	// the initial value to set the port to, when writing
	bool			bContinuous;	// when reading, whether it occurs continuously
} SChanInitMCDAQ;


/** Struct used for requesting a write from the server. **/
typedef struct SMCDAQWData
{
	unsigned short	usValue;	// the value to output.
	/*	Which of the output bits to update. This lets you update only some of the output bits
		while not changing the others. For instance, if you only want to update bit 3 with this
		struct, then you set usBitSelect to 0x0008 and only bit 3 will be changed. The other
		bits will remain as is.*/
	unsigned short	usBitSelect;
} SMCDAQWData;



#endif
