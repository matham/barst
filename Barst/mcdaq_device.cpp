/**
For MCDAQ channels, after a channel is created, the channel is always active.
That is you can instantly request read/writes. A activate/inactivate command will
not be recognized, and cause a error response.

To request a read, you just send a trigger, provided the channel was
opened with read permissions. The pipe tha triggered will get a response, or many
responses if the channel is continuous. To stop a continuous read, you
send a cancel request from the pipe that initially send the trigger.
Because you cannot set the channel to inactive, the only way to stop a
continuous read is to cancel it. After a cancel, no new data will
be added to that pipe, the last message, perhaps after data already queue
to be sent, is the cancel request response.
**/

#include "base classses.h"
#include "named pipes.h"
#include "mcdaq_device.h"
#include "misc tools.h"
#include "cbw.h"
#include "WinError.h"

int (EXTCCONV *lpf_cbDOut)(int BoardNum, int PortNum, USHORT DataValue) = NULL;
int (EXTCCONV *lpf_cbDIn)(int BoardNum, int PortNum, USHORT *DataValue) = NULL;
int (EXTCCONV *lpf_cbGetErrMsg)(int ErrCode, char *ErrMsg) = NULL;
int (EXTCCONV *lpf_cbDeclareRevision)(float *RevNum) = NULL;
int (EXTCCONV *lpf_cbGetRevision)(float *RevNum, float *VxDRevNum) = NULL;


DWORD WINAPI MCDAQProc(LPVOID lpParameter)
{
    return ((CChannelMCDAQ *)lpParameter)->ThreadProc();
}


CManagerMCDAQ::CManagerMCDAQ(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError) : 
	m_usChans(GINUMBOARDS + GINUMEXPBOARDS), CManager(MCDAQ_MAN_STR, std::tstring(szPipe), nChan)
{
	nError= 0;
	m_bError= true;
	m_pcComm= NULL;
	m_pcLogBuffer= NULL;
	m_pcMemPool= new CMemPool;
	m_hLib= NULL;

	if (!pcComm || !szPipe)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	BOOL bRes= GetProcAddresses(&m_hLib,
#ifdef _WIN64
		_T("CBW64.dll")
#else
		_T("CBW32.dll")
#endif
		, 5, &lpf_cbDOut, "cbDOut", &lpf_cbDIn, "cbDIn", &lpf_cbGetErrMsg, "cbGetErrMsg",
		&lpf_cbDeclareRevision, "cbDeclareRevision", &lpf_cbGetRevision, "cbGetRevision");
	if (!bRes || !m_hLib)
	{
		nError= DRIVER_ERROR;
		return;
	}
	float fVer = (float)(CURRENTREVNUM);
	if (nError = MCDAQ_ERROR(lpf_cbDeclareRevision(&fVer), nError))
		return;

	m_acDAQDevices.assign(m_usChans, NULL);
	m_bError= false;
	m_pcComm= pcComm;
}

DWORD CManagerMCDAQ::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut);
	if (dwSize<sizeof(SBaseOut))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut);
	((SBaseOut*)pHead)->sBaseIn.eType= eResponseEx;
	((SBaseOut*)pHead)->sBaseIn.nChan= m_nChan;
	((SBaseOut*)pHead)->sBaseIn.nError= 0;
	((SBaseOut*)pHead)->bActive= true;
	_tcsncpy_s(((SBaseOut*)pHead)->szName, DEVICE_NAME_SIZE, m_csName.c_str(), _TRUNCATE);

	return sizeof(SBaseOut);
}

CManagerMCDAQ::~CManagerMCDAQ()
{
	for (size_t i= 0; i<m_usChans; ++i)
		delete m_acDAQDevices[i];
	if (m_hLib != NULL)
        FreeLibrary(m_hLib);
	delete m_pcMemPool;
}

void CManagerMCDAQ::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if (m_bError)
		return;
	SBaseIn* pBase;
	SData sData;
	sData.pDevice= this;
	SBaseIn sBase;
	sBase.dwSize= sizeof(SBaseIn);
	sBase.nChan= -1;
	sBase.nError= 0;
	bool bRes= true;
	if (!pHead || dwSize < sizeof(SBaseIn) || dwSize != ((SBaseIn*)pHead)->dwSize)	// incorrect size read
	{
		sBase.nError= SIZE_MISSMATCH;
	} else if (((SBaseIn*)pHead)->eType == eQuery && dwSize == sizeof(SBaseIn))	// need info
	{
		sBase.eType= eQuery;
		if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acDAQDevices.size() || 
			!m_acDAQDevices[((SBaseIn*)pHead)->nChan])	// invalid channel
		{
			sBase.nError= INVALID_CHANN;
		} else			// send info on particular chann
		{
			bRes= false;
			DWORD dwSizeInfo= m_acDAQDevices[((SBaseIn*)pHead)->nChan]->GetInfo(NULL, 0);
			pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(dwSizeInfo);
			if (pBase)
			{
				m_acDAQDevices[((SBaseIn*)pHead)->nChan]->GetInfo(pBase, dwSizeInfo);
				sData.dwSize= dwSizeInfo;
				sData.pHead= pBase;
				m_pcComm->SendData(&sData, llId);
			}
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eDelete)	// delete a channel
	{
		sBase.eType= eDelete;
		if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acDAQDevices.size() || 
			!m_acDAQDevices[((SBaseIn*)pHead)->nChan])
			sBase.nError= INVALID_CHANN;
		else
		{
			delete m_acDAQDevices[((SBaseIn*)pHead)->nChan];
			m_acDAQDevices[((SBaseIn*)pHead)->nChan]= NULL;
			sBase.nChan= ((SBaseIn*)pHead)->nChan;
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eVersion && 
		((SBaseIn*)pHead)->nChan == -1)
	{
		float fVer1, fVer2;
		sBase.nError= MCDAQ_ERROR(lpf_cbGetRevision(&fVer1, &fVer2), sBase.nError);
		sBase.dwInfo= (DWORD)(fVer1 * 10000);
		sBase.eType= eVersion;
	} else if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acDAQDevices.size())
	{
		sBase.eType= ((SBaseIn*)pHead)->eType;
		sBase.nError= INVALID_CHANN;
	} else if (((SBaseIn*)pHead)->eType == eSet && m_acDAQDevices[((SBaseIn*)pHead)->nChan])
	{
		sBase.eType= ((SBaseIn*)pHead)->eType;
		sBase.nError= ALREADY_OPEN;
	} else if (((SBaseIn*)pHead)->eType == eSet && 
		((SBaseIn*)pHead)->dwSize == sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitMCDAQ) && 
		((SBase*)((char*)pHead+sizeof(SBaseIn)))->eType == eMCDAQChanInit)	// set a channel
	{
		bRes= false;
		LARGE_INTEGER llStart;
		sBase.eType= eSet;	// in case of error we do respond at end
		SChanInitMCDAQ sChanInit= *(SChanInitMCDAQ*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn));
		std::tstringstream ss;	// daq channel
		ss<<((SBaseIn*)pHead)->nChan;
		std::tstringstream ss2;	// daq manager index
		ss2<<m_nChan;
		std::tstring csPipeName= m_csPipeName+_T(":")+ss2.str()+_T(":")+ss.str(); // new channel pipe name
		CChannelMCDAQ* pcChan= NULL;
		if (!sBase.nError)
			pcChan= new CChannelMCDAQ(csPipeName.c_str(), ((SBaseIn*)pHead)->nChan, sChanInit, sBase.nError, llStart);
		if (!sBase.nError)
		{
			m_acDAQDevices[((SBaseIn*)pHead)->nChan]= pcChan;
			SBaseOut* pBaseO= (SBaseOut*)m_pcMemPool->PoolAcquire(sizeof(SBaseOut));
			if (pBaseO)
			{
				pBaseO->sBaseIn.dwSize= sizeof(SBaseOut);
				pBaseO->sBaseIn.eType= eResponseExL;
				pBaseO->sBaseIn.nChan= ((SBaseIn*)pHead)->nChan;
				pBaseO->sBaseIn.nError= 0;
				pBaseO->llLargeInteger= llStart;
				pBaseO->bActive= true;
				sData.pHead= pBaseO;
				sData.dwSize= pBaseO->sBaseIn.dwSize;
				m_pcComm->SendData(&sData, llId);
			}
		} else
			delete pcChan;
	} else
		sBase.nError= INVALID_COMMAND;

	if (sBase.nError || bRes)
	{
		sData.pHead= (SBaseIn*)m_pcMemPool->PoolAcquire(sizeof(SBaseIn));
		if (sData.pHead)
		{
			sData.dwSize= sizeof(SBaseIn);
			memcpy(sData.pHead, &sBase, sizeof(SBaseIn));
			m_pcComm->SendData(&sData, llId);
		}
	}
}





CChannelMCDAQ::CChannelMCDAQ(const TCHAR szPipe[], int nChan, SChanInitMCDAQ &sChanInit, int &nError, 
		LARGE_INTEGER &llStart) : CDevice(MCDAQ_CHAN_STR), m_csPipeName(szPipe), 
		m_sChanInit(sChanInit), m_usChan(nChan), m_hWriteEvent(CreateEvent(NULL,TRUE, FALSE, NULL)),
		m_asWPackets(m_hWriteEvent)
{
	m_bError= true;
	m_pcComm= NULL;
	m_pcMemPool= new CMemPool;
	m_hStopEvent= NULL;
	InitializeCriticalSection(&m_hReadSafe);
	m_hThread= NULL;
	m_usLastWrite = m_sChanInit.usInitialVal;
	nError= 0;
	if (!szPipe || m_sChanInit.ucDirection > 2)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_hReadEvent = CreateEvent(NULL,TRUE, FALSE, NULL);
	m_hStopEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
	if (!m_hStopEvent || !m_hReadEvent || !m_hWriteEvent)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}

	unsigned short usRead = 0;
	if (m_sChanInit.ucDirection)
		nError= MCDAQ_ERROR(lpf_cbDOut(m_usChan, AUXPORT, m_sChanInit.usInitialVal), nError);
	if (!nError && m_sChanInit.ucDirection != 1)
		nError= MCDAQ_ERROR(lpf_cbDIn(m_usChan, AUXPORT, &usRead), nError);
	if (nError)
		return;

	m_pcComm= new CPipeServer;	// our pipe over which comm to devices will occur
	nError= static_cast<CPipeServer*>(m_pcComm)->Init(szPipe, ~0x80000000, MIN_BUFF_IN,
		MIN_BUFF_OUT, this, NULL);
	if (nError)
		return;
	m_hThread= CreateThread(NULL, 0, MCDAQProc, this, 0, NULL);
	if (!m_hThread)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}
	llStart= m_cTimer.GetStart();
	m_bError= false;
}

DWORD CChannelMCDAQ::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return 2*sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitMCDAQ);
	if (dwSize<2*sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitMCDAQ))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= 2*sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitMCDAQ);
	((SBaseOut*)pHead)->sBaseIn.eType= eResponseEx;
	((SBaseOut*)pHead)->sBaseIn.nChan= m_usChan;
	((SBaseOut*)pHead)->sBaseIn.nError= 0;
	((SBaseOut*)pHead)->bActive= true;
	_tcsncpy_s(((SBaseOut*)pHead)->szName, DEVICE_NAME_SIZE, m_csName.c_str(), _TRUNCATE);
	pHead= (char*)pHead+ sizeof(SBaseOut);

	((SBaseOut*)pHead)->sBaseIn.dwSize = sizeof(SBaseOut);
	((SBaseOut*)pHead)->sBaseIn.eType = eResponseExL;
	((SBaseOut*)pHead)->sBaseIn.nChan = m_usChan;
	((SBaseOut*)pHead)->sBaseIn.nError = 0;
	((SBaseOut*)pHead)->bActive = true;
	((SBaseOut*)pHead)->llLargeInteger = m_cTimer.GetStart();
	pHead = (char*)pHead + sizeof(SBaseOut);

	((SBase*)pHead)->dwSize= sizeof(SChanInitMCDAQ)+sizeof(SBase);
	((SBase*)pHead)->eType= eMCDAQChanInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sChanInit, sizeof(SChanInitMCDAQ));

	return 2*sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitMCDAQ);
}

CChannelMCDAQ::~CChannelMCDAQ()
{
	if (m_hThread && (WAIT_OBJECT_0 != SignalObjectAndWait(m_hStopEvent, m_hThread, 2000, FALSE)))
		TerminateThread(m_hThread, 0);
	if (m_hThread) CloseHandle(m_hThread);
	if (m_pcComm) m_pcComm->Close();

	bool bValid;
	SMCDAQPacket* psPackt;
	while (m_asWPackets.GetSize())
	{
		psPackt= m_asWPackets.Front(true, bValid);
		if (psPackt && bValid)
			delete psPackt->psData;
			delete psPackt;
	}

	if (m_hStopEvent) CloseHandle(m_hStopEvent);
	if (m_hWriteEvent) CloseHandle(m_hWriteEvent);
	if (m_hReadEvent) CloseHandle(m_hReadEvent);
	DeleteCriticalSection(&m_hReadSafe);
	delete m_pcComm;
	delete m_pcMemPool;
}

void CChannelMCDAQ::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if (m_bError)
		return;
	int nError= 0;
	SBaseIn* pBase= (SBaseIn*)pHead;
	if (!pBase || dwSize < sizeof(SBaseIn) || dwSize != pBase->dwSize)	// incorrect size read
		nError= SIZE_MISSMATCH;
	else if (!((pBase->eType == eQuery || ((pBase->eType == eTrigger ||
		pBase->eType == eCancelReadRequest) && m_sChanInit.ucDirection != 1)) && 
		dwSize == sizeof(SBaseIn)) && !(pBase->eType == eData && m_sChanInit.ucDirection &&
		((SBase*)((char*)pBase+sizeof(SBaseIn)))->eType == eMCDAQWriteData &&	// write 
		dwSize == sizeof(SBaseIn)+sizeof(SBase)+sizeof(SMCDAQWData)))
		nError= INVALID_COMMAND;
	else if (pBase->nChan != m_usChan)
		nError= INVALID_CHANN;

	SData sData;
	sData.pDevice= this;
	sData.dwSize= sizeof(SBaseIn);
	if (nError)	// bad read
	{
		pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(sData.dwSize);
		if (pBase)
		{
			pBase->dwSize= sizeof(SBaseIn);
			pBase->eType= eResponse;
			pBase->nChan= -1;
			pBase->nError= nError;
			sData.pHead= pBase;
			m_pcComm->SendData(&sData, llId);
		}
	} else if (pBase->eType == eQuery)	// send back info on channel
	{
		sData.dwSize= GetInfo(NULL, 0);
		sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead && GetInfo(sData.pHead, sData.dwSize) == sData.dwSize)
			m_pcComm->SendData(&sData, llId);
	} else if (pBase->eType == eData)	// need to queue write request
	{
		SMCDAQPacket *psPacket = new SMCDAQPacket;
		psPacket->llId = llId;
		psPacket->psData = new SMCDAQWData;
		*psPacket->psData = *((SMCDAQWData *)((char *)pBase + sizeof(SBaseIn) + sizeof(SBase)));
		m_asWPackets.Push(psPacket);
	} else if (pBase->eType != eTrigger)	// add this read request
	{
		EnterCriticalSection(&m_hReadSafe);
		int k = 0;
		for (; k < m_allReads.size(); ++k)
			if (m_allReads[k] == llId)
				break;
		if (!m_sChanInit.bContinuous || k == m_allReads.size())
		{
			m_allReads.push_back(llId);
			SetEvent(m_hReadEvent);
			k = -1;
		}
		LeaveCriticalSection(&m_hReadSafe);
		if (k != -1)
		{
			pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(sData.dwSize);
			if (pBase)
			{
				pBase->dwSize= sizeof(SBaseIn);
				pBase->eType= ((SBaseIn*)pHead)->eType;
				pBase->nChan= ((SBaseIn*)pHead)->nChan;
				pBase->nError= ALREADY_OPEN;
				sData.pHead= pBase;
				m_pcComm->SendData(&sData, llId);
			}
		}
	} else							// remove this read request
	{
		EnterCriticalSection(&m_hReadSafe);
		for (int k = 0; k < m_allReads.size(); ++k)
		{
			if (m_allReads[k] == llId)
			{
				m_allReads.erase(m_allReads.begin() + k);
				break;
			}
		}
		if (!m_allReads.size())
			ResetEvent(m_hReadEvent);
		LeaveCriticalSection(&m_hReadSafe);
		pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(sData.dwSize);
		if (pBase)
		{
			pBase->dwSize= sData.dwSize;
			pBase->eType= ((SBaseIn*)pHead)->eType;
			pBase->nChan= ((SBaseIn*)pHead)->nChan;
			pBase->nError= 0;
			sData.pHead= pBase;
			m_pcComm->SendData(&sData, llId);
		}
	}
}

DWORD CChannelMCDAQ::ThreadProc()
{
	HANDLE ahEvents[]= {m_hStopEvent, m_hWriteEvent, m_hReadEvent};
	bool bDone= false, bValid;
	DWORD dwWait = INFINITE;
	unsigned short usData;
	SData sData;
	sData.pDevice= this;
	SBaseOut sBaseOut;
	SBaseIn sBaseIn;
	memset(&sBaseOut, 0, sizeof(SBaseOut));
	sBaseOut.bActive= true;
	sBaseOut.sBaseIn.eType= eResponseExD;
	sBaseOut.sBaseIn.nChan= m_usChan;
	sBaseIn.nError = 0;
	sBaseIn.eType = eData;
	sBaseIn.dwSize = sizeof(SBaseIn);
	SMCDAQPacket* psPacket;

	while (!bDone)
	{
		switch (WaitForMultipleObjects(sizeof(ahEvents) / sizeof(HANDLE), &ahEvents[0], FALSE, dwWait))
		{
		case WAIT_OBJECT_0+1:										// user requested write
		{
			ResetEvent(m_hWriteEvent);	// event won't be set again as long as queue is not empty
			psPacket= m_asWPackets.Front(false, bValid);
			if (m_asWPackets.GetSize())
				SetEvent(m_hWriteEvent);
			if (!psPacket || !bValid)	// valid queue element
				break;
			sBaseOut.sBaseIn.nError = MCDAQ_ERROR(lpf_cbDOut(m_usChan, AUXPORT,
				(psPacket->psData->usBitSelect & psPacket->psData->usValue) |
				(~psPacket->psData->usBitSelect & m_usLastWrite)), sBaseOut.sBaseIn.nError);
			sBaseOut.dDouble= g_cTimer.Seconds();
			sData.dwSize= sizeof(SBaseOut);
			sBaseOut.sBaseIn.dwSize= sData.dwSize;
			SBaseOut* pBase= (SBaseOut*)m_pcMemPool->PoolAcquire(sData.dwSize);
			if (pBase)
			{
				sData.pHead= pBase;
				*pBase = sBaseOut;
				m_pcComm->SendData(&sData, psPacket->llId);
			}
			delete psPacket->psData;
			delete psPacket;
			break;
		}
		case WAIT_OBJECT_0 + 2:										// user requested read
		{
			ResetEvent(m_hReadEvent);	// event won't be set again as long as queue is not empty
			sBaseOut.sBaseIn.nError = MCDAQ_ERROR(lpf_cbDIn(m_usChan, AUXPORT, &usData), sBaseOut.sBaseIn.nError);
			sBaseIn.dwInfo = usData;
			sBaseOut.dDouble= g_cTimer.Seconds();
			sData.dwSize = sizeof(SBaseOut) + sizeof(SBaseIn);
			sBaseOut.sBaseIn.dwSize = sData.dwSize;
			EnterCriticalSection(&m_hReadSafe);
			SBaseOut* pBase;
			for (int i = 0; i < m_allReads.size(); ++i)
			{
				pBase = (SBaseOut *)m_pcMemPool->PoolAcquire(sData.dwSize);
				if (pBase)
				{
					sData.pHead = pBase;
					*pBase = sBaseOut;
					*((SBaseIn *)((char *)pBase + sizeof(SBaseOut))) = sBaseIn;
					if (m_pcComm->SendData(&sData, m_allReads[i]))
						m_allReads[i] = -1;
				}
			}
			if (!m_sChanInit.bContinuous)
				m_allReads.clear();
			int k = 0;
			while (k < m_allReads.size())	// remove bad pipes
			{
				if (m_allReads[k] == -1)
					m_allReads.erase(m_allReads.begin() + k);
				else
					k += 1;
			}
			if (m_allReads.size())
				SetEvent(m_hReadEvent);
			LeaveCriticalSection(&m_hReadSafe);
			break;
		}
		case WAIT_OBJECT_0:
		default:
			bDone= true;
			break;
		}
		dwWait= INFINITE;
	}

	sBaseOut.sBaseIn.dwSize= sizeof(SBaseIn);
	sBaseOut.sBaseIn.eType= eResponse;
	sBaseOut.sBaseIn.nError= DEVICE_CLOSING;
	sData.dwSize= sizeof(SBaseIn);
	EnterCriticalSection(&m_hReadSafe);
	for (int i = 0; i < m_allReads.size(); ++i)
	{
		sData.pHead = m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead)
		{
			*(SBaseIn *)sData.pHead = sBaseOut.sBaseIn;
			m_pcComm->SendData(&sData, m_allReads[i]);
		}
	}
	m_allReads.clear();
	LeaveCriticalSection(&m_hReadSafe);
	while (m_asWPackets.GetSize())
	{
		psPacket = m_asWPackets.Front(true, bValid);
		if (!psPacket || !bValid)
			continue;
		sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead)
		{
			*(SBaseIn *)sData.pHead = sBaseOut.sBaseIn;
			m_pcComm->SendData(&sData, psPacket->llId);
		}
		delete psPacket->psData;
		delete psPacket;
	}
	return 0;
}
