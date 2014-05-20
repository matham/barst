
#include "cpl defs.h"
#include <Windows.h>
#include "serial device.h"
#include "named pipes.h"
#include "misc tools.h"
#include <string>
#include <iostream>

BOOL (WINAPI *lpf_CancelIoEx)(HANDLE hFile, LPOVERLAPPED lpOverlapped)= NULL;	// only availible in >=Vista
BOOL (WINAPI *lpf_CancelIo)(HANDLE hFile)= NULL;




DWORD WINAPI SerialProc(LPVOID lpParameter)
{
	// Call ThreadProc function of pipe object
    return ((CChannelSerial*)lpParameter)->ThreadProc();
}



CManagerSerial::CManagerSerial(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError) : 
	CManager(SERIAL_MAN_STR, std::tstring(szPipe), nChan)
{
	nError= 0;
	m_bError= true;
	m_pcComm= NULL;
	m_hLib= NULL;
	m_pcLogBuffer= NULL;
	m_pcMemPool= new CMemPool;
	
	if (!pcComm || !szPipe)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_hLib= LoadLibrary(_T("kernel32.dll"));
	if (!m_hLib)
	{
		nError= LIBRARY_ERROR;
		return;
	}
	*(FARPROC*)&lpf_CancelIoEx= GetProcAddress(m_hLib, _T("CancelIoEx"));
	*(FARPROC*)&lpf_CancelIo= GetProcAddress(m_hLib, _T("CancelIo"));
	if (!lpf_CancelIoEx && !lpf_CancelIo)
	{
		nError= LIBRARY_ERROR;
		return;
	}
	
	m_bError= false;
	m_pcComm= pcComm;
}

DWORD CManagerSerial::GetInfo(void* pHead, DWORD dwSize)
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

CManagerSerial::~CManagerSerial()
{
	for (size_t i= 0; i<m_acSerialDevices.size(); ++i)
		delete m_acSerialDevices[i];
	delete m_pcMemPool;
	FreeLibrary(m_hLib);
	lpf_CancelIoEx= NULL;
	lpf_CancelIo= NULL;
}

void CManagerSerial::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
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
		if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acSerialDevices.size() || 
			!m_acSerialDevices[((SBaseIn*)pHead)->nChan])	// invalid channel
		{
			sBase.nError= INVALID_CHANN;
		} else			// send info on particular chann
		{
			bRes= false;
			DWORD dwSizeInfo= m_acSerialDevices[((SBaseIn*)pHead)->nChan]->GetInfo(NULL, 0);
			pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(dwSizeInfo);
			if (pBase)
			{
				m_acSerialDevices[((SBaseIn*)pHead)->nChan]->GetInfo(pBase, dwSizeInfo);
				sData.dwSize= dwSizeInfo;
				sData.pHead= pBase;
				m_pcComm->SendData(&sData, llId);
			}
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eDelete)	// delete a channel
	{
		sBase.eType= eDelete;
		if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acSerialDevices.size() || 
			!m_acSerialDevices[((SBaseIn*)pHead)->nChan])
			sBase.nError= INVALID_CHANN;
		else
		{
			delete m_acSerialDevices[((SBaseIn*)pHead)->nChan];
			m_acSerialDevices[((SBaseIn*)pHead)->nChan]= NULL;
			sBase.nChan= ((SBaseIn*)pHead)->nChan;
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eVersion && 
		((SBaseIn*)pHead)->nChan == -1)
	{
		sBase.nError= 0;
		sBase.dwInfo= GetVersion();
		sBase.eType= eVersion;
	} else if (((SBaseIn*)pHead)->eType == eSet && 
		((SBaseIn*)pHead)->dwSize == sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitSerial) && 
		((SBase*)((char*)pHead+sizeof(SBaseIn)))->eType == eSerialChanInit)	// set a channel
	{
		bRes= false;
		LARGE_INTEGER llStart;
		sBase.eType= eSet;	// in case of error we do respond at end
		SChanInitSerial sChanInit= *(SChanInitSerial*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn));
		size_t i= 0;
		for (; i<m_acSerialDevices.size() && m_acSerialDevices[i]; ++i);	// get index where we add new channel
		if (i == m_acSerialDevices.size())
			m_acSerialDevices.push_back(NULL);

		std::tstringstream ss;	// channel
		ss<<i;
		std::tstringstream ss2;	// manager index
		ss2<<m_nChan;
		std::tstring csPipeName= m_csPipeName+_T(":")+ss2.str()+_T(":")+ss.str(); // new channel pipe name
		CChannelSerial* pcChan= new CChannelSerial(csPipeName.c_str(), (int)i, sChanInit, sBase.nError, llStart);
		if (!sBase.nError)
		{
			m_acSerialDevices[i]= pcChan;
			SBaseOut* pBaseO= (SBaseOut*)m_pcMemPool->PoolAcquire(sizeof(SBaseOut));
			if (pBaseO)
			{
				pBaseO->sBaseIn.dwSize= sizeof(SBaseOut);
				pBaseO->sBaseIn.eType= eResponseExL;
				pBaseO->sBaseIn.nChan= (int)i;
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




CChannelSerial::CChannelSerial(const TCHAR szPipe[], int nChan, SChanInitSerial &sChanInit, int &nError, 
		LARGE_INTEGER &llStart) : CDevice(SERIAL_CHAN_STR), m_csPipeName(szPipe), 
		m_sChanInit(sChanInit), m_usChan(nChan), m_hWriteEvent(CreateEvent(NULL,TRUE, FALSE, NULL)),
		m_asWPackets(m_hWriteEvent), m_hReadEvent(CreateEvent(NULL,TRUE, FALSE, NULL)), 
		m_asRPackets(m_hReadEvent)
{
	m_bError= true;
	int nRes= 0;
	m_pcComm= NULL;
	m_hStopEvent= NULL;
	m_acReadBuffer= NULL;
	m_hThread= NULL;
	m_hPort= NULL;
	memset(&m_sWOverlapped, 0, sizeof(OVERLAPPED));
	memset(&m_sROverlapped, 0, sizeof(OVERLAPPED));
	m_pcMemPool= new CMemPool;
	nError= 0;
	if (!szPipe || m_sChanInit.ucByteSize < 4 || m_sChanInit.ucByteSize > 8 || 
		m_sChanInit.ucParity > 4 || m_sChanInit.ucStopBits > 2 ||
		(!m_sChanInit.dwMaxStrRead && !m_sChanInit.dwMaxStrWrite))
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_hStopEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
	m_sWOverlapped.hEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
	m_sROverlapped.hEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
	m_acReadBuffer= (char*)m_pcMemPool->PoolAcquire(sizeof(char)*m_sChanInit.dwMaxStrRead);
	if (!m_hStopEvent || !m_hWriteEvent || !m_hReadEvent || 
		!m_sWOverlapped.hEvent || !m_sROverlapped.hEvent || !m_acReadBuffer)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}

	std::tstring csName= m_sChanInit.szPortName;
	csName= "\\\\.\\"+csName;
	// open port
	m_hPort= CreateFile(csName.c_str(), GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (m_hPort == INVALID_HANDLE_VALUE || !m_hPort)
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return;
	}
	DCB sDcb;
	memset(&sDcb, 0, sizeof(DCB));
	sDcb.DCBlength= sizeof(DCB);
	if (!GetCommState(m_hPort, &sDcb))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return;
	}
	sDcb.BaudRate= m_sChanInit.dwBaudRate;
	sDcb.Parity= m_sChanInit.ucParity;
	sDcb.ByteSize= m_sChanInit.ucByteSize;
	sDcb.StopBits= m_sChanInit.ucStopBits;
	sDcb.fRtsControl= RTS_CONTROL_DISABLE;
	sDcb.fDtrControl= DTR_CONTROL_DISABLE;
	sDcb.EofChar= 0x1A;
	sDcb.EvtChar= 0x1A;
	sDcb.XonChar= 0x11;
	sDcb.XoffChar= 0x13;
	sDcb.XonLim= 1024;
	sDcb.XoffLim= 1024;
	if (!SetCommState(m_hPort, &sDcb))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return;
	}
	if (!SetCommMask(m_hPort, EV_RXCHAR))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return;
	}
	if (!GetCommTimeouts(m_hPort, &m_sTimeouts))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return;
	}
	COMMTIMEOUTS sTimeouts;
	sTimeouts.ReadIntervalTimeout = MAXDWORD; 
	sTimeouts.ReadTotalTimeoutMultiplier = 0;
	sTimeouts.ReadTotalTimeoutConstant = 0;
	sTimeouts.WriteTotalTimeoutMultiplier = 0;
	sTimeouts.WriteTotalTimeoutConstant = 0;
	if (!SetCommTimeouts(m_hPort, &sTimeouts))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return;
	}
	
	m_pcComm= new CPipeServer;	// our pipe over which comm to devices will occur
	nError= static_cast<CPipeServer*>(m_pcComm)->Init(szPipe, ~0x80000000, MIN_BUFF_IN+m_sChanInit.dwMaxStrWrite+
		sizeof(SBase)+sizeof(SSerialData)+sizeof(SBaseIn), MIN_BUFF_OUT+m_sChanInit.dwMaxStrRead+
		sizeof(SBase)+sizeof(SSerialData)+sizeof(SBaseOut), this, NULL);
	if (nError)
		return;
	m_hThread= CreateThread(NULL, 0, SerialProc, this, 0, NULL);
	if (!m_hThread)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}
	llStart= m_cTimer.GetStart();
	m_bError= false;
}

DWORD CChannelSerial::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitSerial);
	if (dwSize<sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitSerial))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitSerial);
	((SBaseOut*)pHead)->sBaseIn.eType= eResponseEx;
	((SBaseOut*)pHead)->sBaseIn.nChan= m_usChan;
	((SBaseOut*)pHead)->sBaseIn.nError= 0;
	((SBaseOut*)pHead)->bActive= true;
	_tcsncpy_s(((SBaseOut*)pHead)->szName, DEVICE_NAME_SIZE, m_csName.c_str(), _TRUNCATE);
	pHead= (char*)pHead+ sizeof(SBaseOut);

	((SBase*)pHead)->dwSize= sizeof(SChanInitSerial)+sizeof(SBase);
	((SBase*)pHead)->eType= eSerialChanInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sChanInit, sizeof(SChanInitSerial));

	return sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitSerial);
}

CChannelSerial::~CChannelSerial()
{
	if (m_hThread && (WAIT_OBJECT_0 != SignalObjectAndWait(m_hStopEvent, m_hThread, 2000, FALSE)))
		TerminateThread(m_hThread, 0);
	if (m_hThread) CloseHandle(m_hThread);
	if (m_hPort) PurgeComm(m_hPort, PURGE_RXABORT|PURGE_RXCLEAR|PURGE_TXABORT|PURGE_TXCLEAR);
	if (m_hPort) SetCommTimeouts(m_hPort, &m_sTimeouts);
	if (m_hPort) CloseHandle(m_hPort);
	if (m_pcComm) m_pcComm->Close();
	bool bValid;
	SSerialPacket* psPackt;
	while (m_asRPackets.GetSize())
	{
		psPackt= m_asRPackets.Front(true, bValid);
		if (psPackt && bValid)
		{
			m_pcMemPool->PoolRelease(psPackt->psSerialData);
			delete psPackt;
		}
	}
	while (m_asWPackets.GetSize())
	{
		psPackt= m_asWPackets.Front(true, bValid);
		if (psPackt && bValid)
		{
			m_pcMemPool->PoolRelease(psPackt->psSerialData);
			delete psPackt;
		}
	}
	if (m_sROverlapped.hEvent) CloseHandle(m_sROverlapped.hEvent);
	if (m_sWOverlapped.hEvent) CloseHandle(m_sWOverlapped.hEvent);
	if (m_hStopEvent) CloseHandle(m_hStopEvent);
	if (m_hWriteEvent) CloseHandle(m_hWriteEvent);
	if (m_hReadEvent) CloseHandle(m_hReadEvent);
	m_pcMemPool->PoolRelease(m_acReadBuffer);
	delete m_pcComm;
	delete m_pcMemPool;
}

void CChannelSerial::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if (m_bError)
		return;
	int nError= 0;
	SBaseIn* pBase= (SBaseIn*)pHead;
	if (!pBase || dwSize < sizeof(SBaseIn) || dwSize != pBase->dwSize)	// incorrect size read
		nError= SIZE_MISSMATCH;
	else if (pBase->nChan != m_usChan)
		nError= INVALID_CHANN;
	else if (!(dwSize == sizeof(SBaseIn) && pBase->eType == eQuery) &&		// query
		!(pBase->eType == eData && 
		((SBase*)((char*)pBase+sizeof(SBaseIn)))->eType == eSerialWriteData &&	// write 
		dwSize == sizeof(SBaseIn)+sizeof(SBase)+sizeof(SSerialData)+		// correct size
		((SSerialData*)((char*)pBase+sizeof(SBaseIn)+sizeof(SBase)))->dwSize) && 
		!(pBase->eType == eTrigger && 
		((SBase*)((char*)pBase+sizeof(SBaseIn)))->eType == eSerialReadData &&	// read 
		dwSize == sizeof(SBaseIn)+sizeof(SBase)+sizeof(SSerialData)))		// correct size
		nError= INVALID_COMMAND;
	else if ((pBase->eType == eTrigger && 
		(((SSerialData*)((char*)pBase+sizeof(SBaseIn)+sizeof(SBase)))->dwSize > m_sChanInit.dwMaxStrRead || 
		((SSerialData*)((char*)pBase+sizeof(SBaseIn)+sizeof(SBase)))->dwSize == 0)) ||
		(pBase->eType == eData && 
		(((SSerialData*)((char*)pBase+sizeof(SBaseIn)+sizeof(SBase)))->dwSize > m_sChanInit.dwMaxStrWrite || 
		((SSerialData*)((char*)pBase+sizeof(SBaseIn)+sizeof(SBase)))->dwSize == 0)))
		nError= BAD_INPUT_PARAMS;

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
	} else if (pBase->eType == eQuery)	// send back info on devices
	{
		sData.dwSize= GetInfo(NULL, 0);
		sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead && GetInfo(sData.pHead, sData.dwSize) == sData.dwSize)
			m_pcComm->SendData(&sData, llId);
	} else							// start or stop device, respond with ok
	{
		SSerialPacket* psPacket= new SSerialPacket;
		psPacket->psSerialData= (SBase*)m_pcMemPool->PoolAcquire(dwSize-sizeof(SBaseIn));
		if (psPacket->psSerialData)
		{
			memcpy(psPacket->psSerialData, (char*)pHead+sizeof(SBaseIn), dwSize-sizeof(SBaseIn));
			psPacket->llId= llId;
			if (pBase->eType == eTrigger)	// read
				m_asRPackets.Push(psPacket);
			else							// write
				m_asWPackets.Push(psPacket);
		}
	}
}

DWORD CChannelSerial::ThreadProc()
{
	HANDLE ahEvents[]= {m_hStopEvent, m_hWriteEvent, m_hReadEvent, m_sWOverlapped.hEvent, m_sROverlapped.hEvent};
	DWORD dwToRead= 0, dwRead= 0, dwToWrite= 0, dwWrote= 0, dwError, dwWait= INFINITE, dwMask= 0;
	double dWStart, dRStart, dWDur= 0, dRDur= 0, dCurrTime;
	bool bDone= false, bValid, bWClear= false, bRClear= false, bRPending= false;
	SData sData;
	sData.pDevice= this;
	SBaseOut sBaseOut;
	memset(&sBaseOut, 0, sizeof(SBaseOut));
	sBaseOut.bActive= true;
	sBaseOut.sBaseIn.eType= eResponseExD;
	sBaseOut.sBaseIn.nChan= m_usChan;
	SSerialPacket* sPacket;
	int nPos= 0;
	
	while (!bDone)
	{
		switch (WaitForMultipleObjects(sizeof(ahEvents)/sizeof(HANDLE), &ahEvents[0], FALSE, dwWait))
		{
		case WAIT_OBJECT_0+1:										// user requested write
		{
			bWClear= true;
			ResetEvent(m_hWriteEvent);	// event won't be set again as long as queue is not empty
			sPacket= m_asWPackets.Front(false, bValid);
			if (!sPacket || !bValid)	// valid queue element
				break;
			dwToWrite= ((SSerialData*)((char*)sPacket->psSerialData+sizeof(SBase)))->dwSize;	// amount to write
			if (!WriteFile(m_hPort, (char*)sPacket->psSerialData+sizeof(SBase)+sizeof(SSerialData), 
				dwToWrite, &dwWrote, &m_sWOverlapped))			// didn't complete immediately
			{
				if ((dwError= GetLastError()) != ERROR_IO_PENDING)	// failed
					sBaseOut.sBaseIn.nError= WIN_ERROR(dwError, sBaseOut.sBaseIn.nError);
				else												// pending
				{
					bWClear= false;
					dWDur= (double)((SSerialData*)((char*)sPacket->psSerialData+sizeof(SBase)))->dwTimeout/1000.0;
					dWStart= m_cTimer.Seconds();
				}
			} else													// completed immediately
			{
				if (dwToWrite != dwWrote)							// failed
					sBaseOut.sBaseIn.nError= RW_FAILED;
			}
			break;
		}
		case WAIT_OBJECT_0+3:										// write finished
		{
			bWClear= true;
			ResetEvent(m_sWOverlapped.hEvent);
			sPacket= m_asWPackets.Front(false, bValid);
			if (!sPacket || !bValid)
				break;
			if (GetOverlappedResult(m_hPort, &m_sWOverlapped, &dwWrote, FALSE))	// success
			{
				if (dwToWrite != dwWrote)							// failed
					sBaseOut.sBaseIn.nError= RW_FAILED;
			}
			else
				sBaseOut.sBaseIn.nError= WIN_ERROR(GetLastError(), sBaseOut.sBaseIn.nError);
			break;
		}
		case WAIT_OBJECT_0+2:
		{
			bRClear= true;
			ResetEvent(m_hReadEvent);	// event won't be set again as long as queue is not empty
			sPacket= m_asRPackets.Front(false, bValid);
			if (!sPacket || !bValid)	// valid queue element
				break;
			dwMask= 0;
			if (!WaitCommEvent(m_hPort, &dwMask, &m_sROverlapped))		// failed
			{
				dwError= GetLastError();
				if (dwError != ERROR_IO_PENDING)
				{
					sBaseOut.sBaseIn.nError= WIN_ERROR(dwError, sBaseOut.sBaseIn.nError);
					break;
				}
				bRPending= true;
			} else
				SetEvent(m_sROverlapped.hEvent);	// read right after this
			dRDur= ((SSerialData*)((char*)sPacket->psSerialData+sizeof(SBase)))->dwTimeout/1000.0;
			dRStart= m_cTimer.Seconds();
			bRClear= false;
			break;
		}
		case WAIT_OBJECT_0+4:
		{
			bRClear= true;
			ResetEvent(m_sROverlapped.hEvent);
			sPacket= m_asRPackets.Front(false, bValid);
			if (!sPacket || !bValid)
				break;
			if (bRPending && !GetOverlappedResult(m_hPort, &m_sROverlapped, &dwError, FALSE))	// failed, only call if pending
			{
				sBaseOut.sBaseIn.nError= WIN_ERROR(GetLastError(), sBaseOut.sBaseIn.nError);	// done with error
				break;
			}
			dwError= 0;
			if (!ClearCommError(m_hPort, &dwError, &m_sComStat))
			{
				sBaseOut.sBaseIn.nError= WIN_ERROR(GetLastError(), sBaseOut.sBaseIn.nError);
				break;
			}
			if (bRPending && !(dwMask&EV_RXCHAR))
			{
				sBaseOut.sBaseIn.nError= RW_FAILED;
				break;
			}
			dwRead= 0;
			if (!ReadFile(m_hPort, &m_acReadBuffer[nPos], min(m_sComStat.cbInQue, 
				((SSerialData*)((char*)sPacket->psSerialData+sizeof(SBase)))->dwSize-nPos), 
				&dwRead, &m_sROverlapped))			// failed
			{
				sBaseOut.sBaseIn.nError= WIN_ERROR(GetLastError(), sBaseOut.sBaseIn.nError);
				break;
			}
			bool bCompleted= false;
			if (((SSerialData*)((char*)sPacket->psSerialData+sizeof(SBase)))->bStop)
			{
				int i= nPos;
				for (; i<nPos+(int)dwRead && (m_acReadBuffer[i]!=((SSerialData*)((char*)sPacket->psSerialData+sizeof(SBase)))->cStop);++i);
				bCompleted= i<nPos+(int)dwRead;
			} else
				bCompleted= nPos+dwRead == ((SSerialData*)((char*)sPacket->psSerialData+sizeof(SBase)))->dwSize;
			nPos+= dwRead;
			if (dwError)	// if there was an error, finish
			{
				sBaseOut.sBaseIn.nError= RW_FAILED;
				bCompleted= true;
			}
			if (!bCompleted)	// not done, read again
			{
				bRPending= false;
				if (!WaitCommEvent(m_hPort, &dwMask,&m_sROverlapped))
				{
					dwError= GetLastError();
					if (dwError != ERROR_IO_PENDING)			// failed
					{
						sBaseOut.sBaseIn.nError= WIN_ERROR(GetLastError(), sBaseOut.sBaseIn.nError);
						break;
					}
					// pending, but if there's data now in read buffer, do another read so not to miss data
					dwError= 0;
					if (!ClearCommError(m_hPort, &dwError, &m_sComStat))
					{
						sBaseOut.sBaseIn.nError= WIN_ERROR(GetLastError(), sBaseOut.sBaseIn.nError);
						break;
					}
					if (dwError)	// if there was an error, finish
					{
						sBaseOut.sBaseIn.nError= RW_FAILED;
						break;
					}
					if (m_sComStat.cbInQue)		// don't call getoverlapped yet because we'll just do a read
						SetEvent(m_sROverlapped.hEvent);
					else
						bRPending= true;	// now we're pending
				} else										// success, set event so we do another read
					SetEvent(m_sROverlapped.hEvent);
				bRClear= false;
			}
			break;
		}
		case WAIT_TIMEOUT:
		{
			dCurrTime= m_cTimer.Seconds();
			if (lpf_CancelIoEx)	// OS >= Vista
			{
				if (dWDur && dCurrTime > dWDur+dWStart)
					lpf_CancelIoEx(m_hPort, &m_sWOverlapped);
				if (dRDur && dCurrTime > dRDur+dRStart)
					lpf_CancelIoEx(m_hPort, &m_sROverlapped);
			} else if (lpf_CancelIo)	// WinXP
			{
				if (dWDur && dCurrTime > dWDur+dWStart)
					lpf_CancelIo(m_hPort);
				if (dRDur && dCurrTime > dRDur+dRStart)
					lpf_CancelIo(m_hPort);
			}
			break;
		}
		case WAIT_OBJECT_0:
		default:
			bDone= true;
			break;
		}

		if (bRClear)									// respond to client, only from reads
		{
			sPacket= m_asRPackets.Front(true, bValid);
			if (m_asRPackets.GetSize())
				SetEvent(m_hReadEvent);
			if (sPacket && bValid)
			{
				sBaseOut.dDouble= m_cTimer.Seconds();
				sData.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData)+sizeof(char)*nPos;
				sBaseOut.sBaseIn.dwSize= sData.dwSize;
				SBaseOut* pBase= (SBaseOut*)m_pcMemPool->PoolAcquire(sData.dwSize);
				if (pBase)
				{
					sData.pHead= pBase;
					memcpy(pBase, &sBaseOut, sizeof(SBaseOut));
					((SBase*)((char*)pBase+sizeof(SBaseOut)))->dwSize= sData.dwSize-sizeof(SBaseOut);
					((SBase*)((char*)pBase+sizeof(SBaseOut)))->eType= eSerialReadData;
					((SSerialData*)((char*)pBase+sizeof(SBaseOut)+sizeof(SBase)))->dwSize= nPos;
					memcpy((char*)pBase+sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData), m_acReadBuffer, sizeof(char)*nPos);
					m_pcComm->SendData(&sData, sPacket->llId);
				}
				m_pcMemPool->PoolRelease(sPacket->psSerialData);
				delete sPacket;
			}
			bRClear= false;
			nPos= 0;
			dRDur= 0;
			bRPending= false;
		}
		if (bWClear)									// respond to client, only from writes
		{
			sPacket= m_asWPackets.Front(true, bValid);
			if (m_asWPackets.GetSize())
				SetEvent(m_hWriteEvent);
			if (sPacket && bValid)
			{
				sBaseOut.dDouble= m_cTimer.Seconds();
				sData.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData);
				sBaseOut.sBaseIn.dwSize= sData.dwSize;
				SBaseOut* pBase= (SBaseOut*)m_pcMemPool->PoolAcquire(sData.dwSize);
				if (pBase)
				{
					sData.pHead= pBase;
					memcpy(pBase, &sBaseOut, sizeof(SBaseOut));
					((SBase*)((char*)pBase+sizeof(SBaseOut)))->dwSize= sizeof(SBase)+sizeof(SSerialData);
					((SBase*)((char*)pBase+sizeof(SBaseOut)))->eType= eSerialWriteData;
					((SSerialData*)((char*)pBase+sizeof(SBaseOut)+sizeof(SBase)))->dwSize= dwWrote;
					m_pcComm->SendData(&sData, sPacket->llId);
				}
				m_pcMemPool->PoolRelease(sPacket->psSerialData);
				delete sPacket;
			}
			dWDur= 0;
			dwWrote= 0;
			bWClear= false;
		}
		sBaseOut.sBaseIn.nError= 0;
		dwWait= INFINITE;
		dCurrTime= m_cTimer.Seconds();
		if (dWDur)
			dwWait= (DWORD)((dWDur-(dCurrTime-dWStart))<0.004?4:ceil(1000*(dWDur-(dCurrTime-dWStart))));
		if (dRDur)
			dwWait= (DWORD)min((dRDur-(dCurrTime-dRStart))<0.004?4:ceil(1000*(dRDur-(dCurrTime-dRStart))), dwWait);
	}

	sBaseOut.sBaseIn.dwSize= sizeof(SBaseIn);
	sBaseOut.sBaseIn.eType= eResponse;
	sBaseOut.sBaseIn.nError= DEVICE_CLOSING;
	sData.dwSize= sizeof(SBaseIn);
	while (m_asRPackets.GetSize())
	{
		sPacket= m_asRPackets.Front(true, bValid);
		if (!sPacket || !bValid)
			continue;
		sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead)
		{
			memcpy(sData.pHead, &sBaseOut.sBaseIn, sData.dwSize);
			m_pcComm->SendData(&sData, sPacket->llId);
		}
		m_pcMemPool->PoolRelease(sPacket->psSerialData);
		delete sPacket;
	}
	while (m_asWPackets.GetSize())
	{
		sPacket= m_asWPackets.Front(true, bValid);
		if (!sPacket || !bValid)
			continue;
		sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead)
		{
			memcpy(sData.pHead, &sBaseOut.sBaseIn, sData.dwSize);
			m_pcComm->SendData(&sData, sPacket->llId);
		}
		m_pcMemPool->PoolRelease(sPacket->psSerialData);
		delete sPacket;
	}
	return 0;
}