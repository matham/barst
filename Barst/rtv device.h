/** Defines the RTV manager and channels. */


#ifndef _CPL_RTV_DEVICE_H_
#define _CPL_RTV_DEVICE_H_

#include "base classses.h"
#include "cpl queue.h"
#include "misc tools.h"


class CChannelRTV;

/** Defines the RTV manager. It loads the AngeloRTV.dll file. */
#define RTV_MAN_STR	_T("RTVMan")
class CManagerRTV : public CManager
{
public:
	CManagerRTV(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError);
	virtual ~CManagerRTV();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	virtual DWORD GetInfo(void* pHead, DWORD dwSize);

	/** This function is called by the RTV dll when a new frame is availible for a channel. */
	void NextFrame(unsigned char* aucData, unsigned short usChan);
private:
	std::vector<CChannelRTV*>		m_acRTVDevices;	// list of all the RTV channels acive, could be NULL
	std::vector<CRITICAL_SECTION*>	m_ahCallbackSafe;	// protects each channel.
	unsigned short					m_usChans;	// number of RTV ports availible for use.
};


/** Defines an RTV channel. */
#define RTV_CHAN_STR	_T("RTVChan")
class CChannelRTV : public CDevice
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
	CChannelRTV(const TCHAR szPipe[], int nChan, SChanInitRTV &sChanInit, int &nError, 
		LARGE_INTEGER &llStart);
	virtual ~CChannelRTV();
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass);
	DWORD GetInfo(void* pHead, DWORD dwSize);
	/** The manager calls this when it has a new frame for this channel. */
	void NextFrame(unsigned char* aucData);
	
	const SChanInitRTV			m_sChanInit;	// channel initialization info
	const std::tstring			m_csPipeName;	// channels pipe name
	const unsigned short		m_usChan;		// channel/RTV channel for this instance
private:
	CTimer						m_cTimer;		// timer of this channel
	bool						m_bActive;		// if the channels is currently active
	long long					m_llId;			// the comm ID to which we'll send new frames

	CRITICAL_SECTION			m_hSentSafe;	// protects access to channel memory objects
	// true if a frame was sent to comm but comm hasn't notified us that it faild or succeeded.
	// this means that if we don't send lossless, a new frame won't be sent while this is true.
	// if sending lossless, we ignore this and just keep adding frames.
	bool						m_bSent;
	// the pointer to the last frame sent if not sending lossless. This is how we keep track
	// to know when the last frame was done sending so we can send a new frame.
	void*						m_pLastSent;
};
#endif