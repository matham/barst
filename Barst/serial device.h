/** Defines the RTV manager and channels. */


#ifndef CPL_SERIAL_DEVICE_H
#define CPL_SERIAL_DEVICE_H

#include "base classses.h"
#include "cpl queue.h"
#include "misc tools.h"


class CChannelSerial;

/** Defines the RTV manager. It loads the AngeloRTV.dll file. */
#define SERIAL_MAN_STR	_T("SerMan")
class CManagerSerial : public CManager
{
public:
	CManagerSerial(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError);
	virtual ~CManagerSerial();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	virtual DWORD GetInfo(void* pHead, DWORD dwSize);

private:
	std::vector<CChannelSerial*>		m_acSerialDevices;	// list of all the RTV channels acive, could be NULL
	HMODULE								m_hLib;
};


typedef struct SSerialPacket
{
	__int64			llId;
	SBase*	psSerialData;
} SSerialPacket;

/** Defines an RTV channel. */
#define SERIAL_CHAN_STR	_T("SerChan")
class CChannelSerial : public CDevice
{
public:
	/** Create an RTV channel for a single RTV port. 
		szPipe is the pipe name that will be exclusivly 
		associated with this channel. The format should Blah:manager#:RTVPort# as defined in the API. 
		nChan is the channel number of this channel. 
		sChanInit initializes the channel. 
		nError returns an error codde if something went wrong.
		llStart returns the offset of the timer for this channel. You can use this to convert the time
		sent with each frame to another common time (see API). */
	CChannelSerial(const TCHAR szPipe[], int nChan, SChanInitSerial &sChanInit, int &nError, 
		LARGE_INTEGER &llStart);
	virtual ~CChannelSerial();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	DWORD GetInfo(void* pHead, DWORD dwSize);

	DWORD ThreadProc();	// thread that reads/writes to physical device
	
	const SChanInitSerial		m_sChanInit;	// channel initialization info
	const std::tstring			m_csPipeName;	// channels pipe name
	const unsigned short		m_usChan;		// channel/RTV channel for this instance
private:
	CTimer						m_cTimer;		// timer of this channel
	HANDLE						m_hPort;		//
	HANDLE						m_hStopEvent;
	HANDLE						m_hWriteEvent;
	HANDLE						m_hReadEvent;
	OVERLAPPED					m_sWOverlapped;
	OVERLAPPED					m_sROverlapped;
	char*						m_acReadBuffer;
	COMMTIMEOUTS				m_sTimeouts;
	COMSTAT						m_sComStat;
	CQueue<SSerialPacket*>		m_asWPackets;
	CQueue<SSerialPacket*>		m_asRPackets;

	HANDLE						m_hThread;
};
#endif