/** Defines the RTV manager and channels. */


#ifndef _CPL_MCDAQ_DEVICE_H_
#define _CPL_MCDAQ_DEVICE_H_

#include "base classses.h"
#include "cpl queue.h"
#include "misc tools.h"


class CChannelMCDAQ;

/** Defines the MCDAQ manager. It loads the CBW32/64.dll file. */
#define MCDAQ_MAN_STR	_T("DAQMan")
class CManagerMCDAQ : public CManager
{
public:
	CManagerMCDAQ(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError);
	virtual ~CManagerMCDAQ();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	virtual DWORD GetInfo(void* pHead, DWORD dwSize);
	const unsigned short			m_usChans;	// number of daq ports availible for use.
private:
	std::vector<CChannelMCDAQ*>		m_acDAQDevices;	// list of all the daq channels acive, could be NULL
};


typedef struct SMCDAQPacket
{
	__int64			llId;
	SMCDAQWData*	psData;
} SMCDAQPacket;


/** Defines an MCDAQ channel. */
#define MCDAQ_CHAN_STR	_T("DAQChan")
class CChannelMCDAQ : public CDevice
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
	CChannelMCDAQ(const TCHAR szPipe[], int nChan, SChanInitMCDAQ &sChanInit, int &nError, 
		LARGE_INTEGER &llStart);
	virtual ~CChannelMCDAQ();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	DWORD GetInfo(void* pHead, DWORD dwSize);

	DWORD ThreadProc();	// thread that reads/writes to physical device
	
	const SChanInitMCDAQ		m_sChanInit;	// channel initialization info
	const std::tstring			m_csPipeName;	// channels pipe name
	const unsigned short		m_usChan;		// channel/RTV channel for this instance
private:
	CTimer						m_cTimer;		// timer of this channel
	unsigned short				m_usLastWrite;

	HANDLE						m_hStopEvent;
	HANDLE						m_hWriteEvent;
	HANDLE						m_hReadEvent;
	CQueue<SMCDAQPacket*>		m_asWPackets;
	std::vector<long long>		m_allReads;
	CRITICAL_SECTION			m_hReadSafe;

	HANDLE						m_hThread;
};
#endif
