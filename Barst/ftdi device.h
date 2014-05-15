/** Defines FTDI devices used. */


#ifndef _CPL_FTDI_DEVICE_H_
#define _CPL_FTDI_DEVICE_H_

#include "base classses.h"
#include "cpl queue.h"
#include "misc tools.h"

class CChannelFTDI;
class CPeriphFTDI;


/** Defines the FTDI manager which controlls all the FTDI devices. */
#define FTDI_MAN_STR	_T("FTDIMan")
class CManagerFTDI : public CManager
{
public:
	CManagerFTDI(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError);
	virtual ~CManagerFTDI();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	virtual DWORD GetInfo(void* pHead, DWORD dwSize);
private:
	std::vector<CChannelFTDI*>		m_acFTDevices;	// holds all the FTDI channels

	DWORD							m_dwNumDevs;	// number of devices discovered (including not ours)
	std::vector<FT_DEVICE_LIST_INFO_NODE> m_asFTChanInfo;	// holds the device info for all the devices discovered
};


// struct used by FTDI thread to send read buffers and its protectors.
typedef struct SFTBufferSafe
{
	CRITICAL_SECTION	sSafe;
	unsigned char*		aucBuff;
} SFTBufferSafe;

// struct used by ftdi thread to repond to user updates commands
typedef struct SFTUpdates
{
	SData sData;
	__int64	llId;
} SFTUpdates;

/*	Defines a FTDI channel. Note, each channel correponds to a single channel on a physical device. But
	each channel can hold multiple virutal devices defined below. */
#define FTDI_CHAN_STR	_T("FTChann")
class CChannelFTDI : public CDevice
{
public:
	/** Creates an FTDI channel. 
		sChanInit initializes the main channel params. pInit is a buffer of size 
		dwSize that holds a list of all the devices used by this channel. Each device has an SBase struct 
		identifying the device type struct following followed by the device init sturct. The channel numbers
		assigned to the devices will be in order of the devices in the buffer. We do not check if multiple
		devices use the same pins (which is invalid). See the API for how we initialize a channel.
		FTInfo is struct describing this channel. 
		szPipe is the pipe name exclusive for this channel (see the API for how they are named). 
		nChan is this channel's number in the FTDI manager.
		nError return an error code if something went wrong.
		llStart returns the offset of the timer for this channel. You can use this to convert the time
		sent with each frame to another common time (see API).*/
	CChannelFTDI(SChanInitFTDI &sChanInit, SBase* pInit, DWORD dwSize, FT_DEVICE_LIST_INFO_NODE &FTInfo, 
		const TCHAR szPipe[], int nChan, int &nError, LARGE_INTEGER &llStart);
	virtual ~CChannelFTDI();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	DWORD GetInfo(void* pHead, DWORD dwSize);

	DWORD ThreadProc();	// thread that reads/writes to physical device

	// channles init data
	const FT_DEVICE_LIST_INFO_NODE	m_FTInfo;
	const std::tstring				m_csPipeName;
	const int						m_nChan;
private:
	FT_HANDLE						m_ftHandle;	// ft_handle to the channel
	std::vector<CPeriphFTDI*>		m_aDevices;	// list of all the subdevices. index is channel number
	SChanInitFTDI					m_sChanInit;


	HANDLE					m_hStop;	// stops the thread
	HANDLE					m_hNext;	// tells the thread to do another read/write
	HANDLE					m_hUpdate;	// tells the thread updates are pending for a device
	std::vector<HANDLE>		m_ahEvents;	// events for all the devices
	HANDLE					m_hThread;	// thread handle
	CQueue<SFTUpdates>			m_asUpdates;	// queue which holds the updates, MUST only be valid


	unsigned char*			m_aucTx;	// buffer used to write to device
	SFTBufferSafe			m_sRx1;		// buffer used to read
	SFTBufferSafe			m_sRx2;		// second buffer used to read

	CTimer					m_cTimer;	// timer for this channel
};



// see the Do work function in CPeriphFTDI for the use of this enum.
enum EStateFTDI
{
	eActive= 1,
	eInactive,
	eActivateState,
	eInactivateState,
	ePreWrite,
	ePostWrite,
	ePostRead,
	eRecover,
};

/*	following are the devices that FTDI channels recognize
	they must be derived from CPeriphFTDI. Channel is initialized with list
	of devices. Every device is initialized with a const struct of the init info. */
class CPeriphFTDI : public CDevice
{
public:
	/** szName is the unique device name. sInitFT is the init for this device. */
	CPeriphFTDI(const TCHAR szName[], const SInitPeriphFT &sInitFT) : CDevice(szName), m_sInitFT(sInitFT),
		m_ucBitOutput(sInitFT.ucBitOutput){};
	virtual ~CPeriphFTDI(){};
	/** The FTDI thread calls this function on all the devices everytime a read/write occurs. However,
		devices only do something if they are active or are in/activated by the this call. This is called
		before every write to give each device the oppertunity to update the buffer to be written. 
		Then after the write it's called again so that the buffer can be set again to the proper values.
		In both cases pHead is the write buffer. 
		The function is also called after a read with pHead pointing to one of the SFTBufferSafe structs
		which holds the most recent buffer read. The device can now extract the data read. 
		Finally, the function is also called when we need to change the state of a device. In that case it's
		only called on the device in question.
		In all cases, dwSize is the size of the buffer (in case of read it's the buffer in the SFTBufferSafe
		struct). 
		ftHandle if the handle to the ft device, you probably shouldn't do anything with it.
		eReason is the reson for this call, such as prewrite etc. Defined in EStateFTDI.
		nError is given the reason for this call if that failed. I.e. if the reason is a post write, it indicates
		if the write failed. 
		The return value has no meaning at the moment. */
	virtual bool DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError)= 0;
	virtual void ProcessData(const void *pHead, DWORD dwSize, __int64 llId)= 0;
	virtual void Result(void *pHead, bool bPass)= 0;
	virtual EStateFTDI GetState()= 0;	// returns if we're active/inactive etc.
	virtual DWORD GetInfo(void* pHead, DWORD dwSize)= 0;
	void SetOutputBits(unsigned char ucBitOutput) {m_ucBitOutput = ucBitOutput;}
	unsigned char GetOutputBits() {return m_ucBitOutput;}

	const SInitPeriphFT		m_sInitFT;
protected:
	EStateFTDI		m_eState;	// current device state
	CTimer*			m_pcTimer;	// timer of device.
	unsigned char	m_ucBitOutput;	// current bits set as output
};


/** defines the ADC device that can be attched to the FTDI bus. */
#define ADC_P	_T("ADCBrd")
#define ADC_RESET_DELAY 5000
enum EHandshakeADC {
	eConfigStart,
	eConfigTO,
	eConfigClkToggle,
	eConfigWrite,
	eConfigDone,
};
class CADCPeriph : public CPeriphFTDI
{
public:
	CADCPeriph(const SADCInit &sADCInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
		HANDLE hNewData, int &nError, CTimer* pcTimer);
	virtual ~CADCPeriph();
	bool DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError);
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	EStateFTDI GetState();
	DWORD GetInfo(void* pHead, DWORD dwSize);

	DWORD ThreadProc();

	const SADCInit			m_sInit;
	const unsigned char		m_ucMask;	// flipped
	const unsigned char		m_ucConfigWriteBit;
	const unsigned char		m_ucConfigReadBit;
	const unsigned char		m_ucReverse;	// 1 if reverse, 0 otherwise
	const unsigned char		m_ucDefault;	// default state of the pins (high/low)
	const unsigned short	m_usConfigWord;
	const unsigned char		m_ucTransPerByte;
	const unsigned char		m_ucNGroups;	// number of transections it takes to get a data set (cycle)
	const unsigned char		m_ucNBytes;		// number of bytes sent in a data set
private:
	void ExtractData();
	DWORD					m_dwRestartE;
	EHandshakeADC			m_eConfigState;
	SData					m_sData;

	HANDLE					m_hThread;
	HANDLE					m_hThreadClose;
	HANDLE					m_hProcessData;
	HANDLE					m_hReset;
	HANDLE					m_hNext;

	SFTBufferSafe*			m_psRx;
	CRITICAL_SECTION		m_hDataSafe;
	CRITICAL_SECTION		m_hStateSafe;
	__int64					m_llId;

	DWORD					m_dwDataCount;	// count of packets since start of now transection cycle
	char					m_cDataOffset;	// number of cycles into m_aucBuff multiplied my m_ucNBytes
	char					m_cBuffSize;
	unsigned char*			m_aucBuff;
	unsigned short			(*m_aucDecoded)[256];
	unsigned char			m_aucGroups[16];	// sending 2 bits at a time w/ 4 bytes per ADC point = max 16 groups
	unsigned char			m_ucFlags;
	unsigned short			m_usBadRead;
	unsigned short			m_usOverflow;
	DWORD					m_dwTempData;
	DWORD					m_dwAmountRead;

	SADCData*				m_psDataHeader;
	DWORD*					m_adwData;
	bool					m_bSecond;
	DWORD					m_dwPos;
	DWORD					m_dwSpaceUsed;
	float					m_fTimeWorked;
	DWORD					m_dwStartRead;
	double					m_dTimeTemp;
	double					m_dTimeS;

	//unsigned char			m_ucLastByte;
};

/** Defines a serial in parallel out shift register that the FTDI channel can write to. */
#define MULTI_W_P	_T("MltWBrd")
class CMultiWPeriph : public CPeriphFTDI
{
public:
	CMultiWPeriph(const SValveInit &sValveInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
		int &nError, HANDLE hNewData, CTimer* pcTimer);
	virtual ~CMultiWPeriph();
	bool DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError);
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	EStateFTDI GetState();
	DWORD GetInfo(void* pHead, DWORD dwSize);

	const SValveInit		m_sInit;
	const unsigned char		m_ucMask;
	const unsigned char		m_ucDefault;
private:
	bool					m_bUpdated;
	bool					m_bChanged;
	int						m_nProcessed;
	double					m_dInitial;
	std::vector<bool>		m_abData;
	CRITICAL_SECTION		m_hDataSafe;
	CQueue<__int64>			m_allIds;
	HANDLE					m_hNext;
};

/** Defines the parallel in serial out shift register that the FTDI channel can read from. */
#define MULTI_R_P	_T("MltRBrd")
class CMultiRPeriph : public CPeriphFTDI
{
public:
	CMultiRPeriph(const SValveInit &sValveInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
		int &nError, HANDLE hNewData, CTimer* pcTimer);
	virtual ~CMultiRPeriph();
	bool DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError);
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	EStateFTDI GetState();
	DWORD GetInfo(void* pHead, DWORD dwSize);

	const SValveInit		m_sInit;
	const unsigned char		m_ucMask;
	const unsigned char		m_ucDefault;
private:
	bool					m_bRead;
	int						m_nProcessed;
	double					m_dInitial;
	CRITICAL_SECTION		m_hDataSafe;
	CQueue<__int64>			m_allIds;
	HANDLE					m_hNext;
};

/** Defines a device which simply lets you write to the individual pins of the FTDI bus (like bit bang). */
#define PIN_W_P	_T("PinWBrd")
class CPinWPeriph : public CPeriphFTDI
{
public:
	CPinWPeriph(const SPinInit &sPinInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
		int &nError, HANDLE hNewData, CTimer* pcTimer);
	virtual ~CPinWPeriph();
	bool DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError);
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	EStateFTDI GetState();
	DWORD GetInfo(void* pHead, DWORD dwSize);

	const SPinInit			m_sInit;
	const unsigned char		m_ucMask;
private:
	unsigned char			m_ucLast;
	bool					m_bUpdated;
	bool					m_bChanged;
	int						m_nProcessed;
	double					m_dInitial;
	std::vector<unsigned char>	m_aucData;
	CRITICAL_SECTION		m_hDataSafe;
	CQueue<__int64>			m_allIds;
	HANDLE					m_hNext;
};

/** Defines a device which simply lets you read from the individual pins of the FTDI bus (like bit bang). */
#define PIN_R_P	_T("PinRBrd")
class CPinRPeriph : public CPeriphFTDI
{
public:
	CPinRPeriph(const SPinInit &sPinInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
		int &nError, HANDLE hNewData, CTimer* pcTimer);
	virtual ~CPinRPeriph();
	bool DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError);
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	EStateFTDI GetState();
	DWORD GetInfo(void* pHead, DWORD dwSize);

	const SPinInit			m_sInit;
	const unsigned char		m_ucMask;
private:
	bool					m_bRead;
	int						m_nProcessed;
	double					m_dInitial;
	CRITICAL_SECTION		m_hDataSafe;
	CQueue<__int64>			m_allIds;
	HANDLE					m_hNext;
};

#endif