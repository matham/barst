
#include "cpl defs.h"
#include "ftdi device.h"



static const unsigned char BitReverseTable256[] = 
{
  0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0, 
  0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, 
  0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4, 
  0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC, 
  0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, 
  0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
  0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6, 
  0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
  0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
  0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9, 
  0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
  0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
  0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3, 
  0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
  0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7, 
  0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};



/////////////////////////////////////////////////
// Starting point of queue thread
DWORD WINAPI ADCProc(LPVOID lpParameter)
{
	// Call ThreadProc function of pipe object
    return ((CADCPeriph*)lpParameter)->ThreadProc();
}




CADCPeriph::CADCPeriph(const SADCInit &sADCInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
	HANDLE hNewData, int &nError, CTimer* pcTimer) : 
	CPeriphFTDI(ADC_P, sInitFT), 
	m_sInit(sADCInit), m_ucDefault(1<<sADCInit.ucClk), m_ucMask(~(1<<sADCInit.ucClk|0xFF<<sADCInit.ucLowestDataBit)),
	m_ucTransPerByte(8/(8-sADCInit.ucLowestDataBit)), m_hNext(hNewData)
{
	nError= 0;
	m_bError= true;
	m_hThreadClose= NULL;
	m_hProcessData= NULL;
	m_hReset= NULL;
	m_hThread= NULL;
	m_psDataHeader= NULL;
	m_pcMemPool= new CMemPool;
	m_pcTimer= NULL;
	InitializeCriticalSection(&m_hDataSafe);
	InitializeCriticalSection(&m_hStateSafe);
	// assumption is that the ADC buffer length is the one that makes dwBuff largest, i.e. ADC buffer is always
	// equal to dwBuff
	if ((m_sInit.ucLowestDataBit != 6 && m_sInit.ucLowestDataBit != 4) || !pcComm || 
		(m_sInitFT.dwBuff%510 && m_sInitFT.dwBuff%62) || m_sInit.ucBitsPerData%8 || m_sInit.ucBitsPerData>24 ||
		(m_sInit.bChan2 && !m_sInit.bStatusReg) || m_sInitFT.dwMinSizeR != m_sInitFT.dwBuff ||
		m_sInitFT.dwMinSizeW != m_sInitFT.dwBuff || !pcComm || !pcTimer || !hNewData)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_pcComm= pcComm;
	m_pcTimer= pcTimer;
	m_hThreadClose= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hProcessData= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hReset= CreateEvent(NULL, TRUE, TRUE, NULL);
	if (!m_hThreadClose || !m_hProcessData || !m_hReset)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}
	m_psDataHeader= (SADCData*) m_pcMemPool->PoolAcquire(sizeof(SADCData)+sizeof(DWORD)*sADCInit.dwDataPerTrans*(sADCInit.bChan2?2:1));
	if (!m_psDataHeader)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}
	m_hThread= CreateThread(NULL, 0, ADCProc, this, 0, NULL);
	if (!m_hThread)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}

	m_psDataHeader->dwPos= 0;
	m_psDataHeader->dwChan2Start= sADCInit.dwDataPerTrans;
	m_psDataHeader->dwCount1= 0;
	m_psDataHeader->dwCount2= 0;
	m_psDataHeader->dwChan1S= 0;
	m_psDataHeader->dwChan2S= 0;
	m_psDataHeader->sDataBase.dwSize=sizeof(SADCData)+sizeof(DWORD)*sADCInit.dwDataPerTrans*(sADCInit.bChan2?2:1);
	m_psDataHeader->sDataBase.eType= eTrigger;
	m_psDataHeader->sDataBase.nChan= m_sInitFT.nChan;
	m_psDataHeader->sBase.dwSize= sizeof(SADCData)+sizeof(DWORD)*sADCInit.dwDataPerTrans*(sADCInit.bChan2?2:1)-sizeof(SBaseIn);
	m_psDataHeader->sBase.eType= eADCData;
	m_psDataHeader->dStartTime= 0;
	m_adwData= (DWORD*)((unsigned char*)m_psDataHeader+sizeof(SADCData));
	m_bPreparing= false;
	m_bSecond= false;
	m_llId= -1;	// so that if we send to closed comm unlikly to have this chann open
	m_dwDataCount= 0;
	m_usBadRead= 0;
	m_usOverflow= 0;
	m_bError= false;
	m_eState= eInactivateState;
	m_dwPos= 0;
	m_sData.pDevice= this;
	m_sData.dwSize= sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*(m_sInit.bChan2?2:1);
};

DWORD CADCPeriph::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SADCInit)+sizeof(SInitPeriphFT);
	if (dwSize<sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SADCInit)+sizeof(SInitPeriphFT))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SADCInit)+sizeof(SInitPeriphFT);;
	((SBaseOut*)pHead)->sBaseIn.eType= eResponseEx;
	((SBaseOut*)pHead)->sBaseIn.nChan= m_sInitFT.nChan;
	((SBaseOut*)pHead)->sBaseIn.nError= 0;
	((SBaseOut*)pHead)->bActive= GetState() == eActive;
	_tcsncpy_s(((SBaseOut*)pHead)->szName, DEVICE_NAME_SIZE, m_csName.c_str(), _TRUNCATE);
	pHead= (char*)pHead+ sizeof(SBaseOut);

	((SBase*)pHead)->dwSize= sizeof(SInitPeriphFT)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIPeriphInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sInitFT, sizeof(SInitPeriphFT));
	pHead= (char*)pHead+ sizeof(SInitPeriphFT);

	((SBase*)pHead)->dwSize= sizeof(SADCInit)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIADCInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sInit, sizeof(SADCInit));

	return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SADCInit)+sizeof(SInitPeriphFT);
}

CADCPeriph::~CADCPeriph()
{
	// wait max 2sec
	if (m_hThread && (WAIT_OBJECT_0 != SignalObjectAndWait(m_hThreadClose, m_hThread, 2000, FALSE)))
		TerminateThread(m_hThread, 0);
	DeleteCriticalSection(&m_hDataSafe);
	DeleteCriticalSection(&m_hStateSafe);
	if (m_hThread) CloseHandle(m_hThread);
	if (m_hProcessData) CloseHandle(m_hProcessData);
	if (m_hReset) CloseHandle(m_hReset);
	if (m_hThreadClose) CloseHandle(m_hThreadClose);
	if (m_psDataHeader)
		m_pcMemPool->PoolRelease(m_psDataHeader);
	delete m_pcMemPool;
}

bool CADCPeriph::DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError)
{
	if (m_bError)
		return true;
	unsigned char *aucBuff= (unsigned char *)pHead;
	switch (eReason)
	{
	case eActivateState:
		EnterCriticalSection(&m_hStateSafe);
		m_eState= eActivateState;
		SetEvent(m_hNext);	// make sure we keep writing even if user forgot to trigger (although we'll send data to -1 ID)
		LeaveCriticalSection(&m_hStateSafe);
		m_bPreparing= false;
		break;
	case eInactivateState:
		EnterCriticalSection(&m_hStateSafe);
		m_eState= eInactivateState;
		m_llId= -1;		// unlikly to have comm with this ID
		LeaveCriticalSection(&m_hStateSafe);
		break;
	case eRecover:
		EnterCriticalSection(&m_hStateSafe);
		if (m_eState == eActive || m_eState == eActivateState)	// start over again
		{
			m_eState= eActivateState;
			m_bPreparing= false;
		}
		LeaveCriticalSection(&m_hStateSafe);
		break;
	case ePreWrite:
		if (m_eState == eActivateState)
		{
			if (!m_bPreparing)	// start anew
			{
				SetEvent(m_hReset);
				m_dwRestartE= GetTickCount()+5000;	// time when device finished reseting
				m_bPreparing= true;
				m_bFirstPrepare= true;	// the first time we clock out initialization seq so it can reset
				DWORD i= 0;
				for (; i<60; ++i)	// initialization seq, (keep in mind min packet is also 62)
				{
					aucBuff[i++]= (aucBuff[i]&m_ucMask)|m_ucDefault;
					aucBuff[i]= aucBuff[i]&m_ucMask;
				}
				for (; i<m_sInitFT.dwBuff;++i)
					aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
			} else	// in the middle of preparing
			{
				if (m_dwRestartE > GetTickCount())	// middle of reset
				{
					if (m_bFirstPrepare)	// the first time remove initialization seq so it can reset
					{
						for (int i= 0; i<60;++i)
							aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
						m_bFirstPrepare= false;
					}
				} else	// ready
				{
					for (DWORD i= 0; i<m_sInitFT.dwBuff-2;++i)
					{
						aucBuff[i++]= (aucBuff[i]&m_ucMask)|m_ucDefault;
						aucBuff[i]= aucBuff[i]&m_ucMask;
					}
					m_bPreparing= false;
					EnterCriticalSection(&m_hStateSafe);
					m_eState= eActive;
					LeaveCriticalSection(&m_hStateSafe);
				}
			}
		} else if (m_eState == eInactivateState)
		{
			m_bPreparing= false;
			SetEvent(m_hReset);
			for (DWORD i= 0; i<m_sInitFT.dwBuff;++i)
				aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
			EnterCriticalSection(&m_hStateSafe);
			m_eState= eInactive;
			ResetEvent(m_hNext);	// stop R/W
			LeaveCriticalSection(&m_hStateSafe);
		} else  if (m_eState == eActive)
		{
			m_dTimeTemp= m_pcTimer->Seconds();	// get start time when we're about to read
		}
		break;
	case ePostRead:
		if (m_eState == eActive)	// select which read buffer to use
		{
			EnterCriticalSection(&m_hDataSafe);
			m_psRx= (SFTBufferSafe*)pHead;
			m_dTimeS= m_dTimeTemp;
			SetEvent(m_hProcessData);
			LeaveCriticalSection(&m_hDataSafe);
		}
		break;
	default:
		break;
	}
	return true;
}

EStateFTDI CADCPeriph::GetState()
{
	EnterCriticalSection(&m_hStateSafe);
	EStateFTDI eState= m_eState;
	LeaveCriticalSection(&m_hStateSafe);
	return eState;
}

void CADCPeriph::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if(m_bError)
		return;
	// only trigger and only accept a trigger for cont if it's the first
	if (!pHead || dwSize != sizeof(SBaseIn) || ((SBaseIn*)pHead)->eType != eTrigger ||
		((SBaseIn*)pHead)->dwSize != dwSize) 
	{
		SData sData;
		sData.dwSize= sizeof(SBaseIn);
		sData.pDevice= this;
		sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseIn));
		if (sData.pHead)
		{
			((SBaseIn*)sData.pHead)->dwSize= sizeof(SBaseIn);
			((SBaseIn*)sData.pHead)->eType= eResponse;
			((SBaseIn*)sData.pHead)->nChan= m_sInitFT.nChan;
			((SBaseIn*)sData.pHead)->nError= INVALID_COMMAND;
			m_pcComm->SendData(&sData, llId);
		}
		return;
	}

	int nError= 0;
	EnterCriticalSection(&m_hStateSafe);
	// XXX activateststae should not be allowed, this can be fixed when ftdi device.cpp line 834 is fixed 
	// (individual devices respond when active so we don't have to guess).
	if (m_eState == eActive || m_eState == eActivateState)
	{
		if (m_llId == -1)	// hasn't been triggered yet (so that we cannot retrigger)
			m_llId= llId;	// pipe to send reads (hNewData has been set when activated)
		else
			nError= ALREADY_OPEN;
	} else
		nError= INACTIVE_DEVICE;
	LeaveCriticalSection(&m_hStateSafe);

	if (nError)
	{
		SData sData;
		sData.dwSize= sizeof(SBaseIn);
		sData.pDevice= this;
		sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseIn));
		if (sData.pHead)
		{
			((SBaseIn*)sData.pHead)->dwSize= sizeof(SBaseIn);
			((SBaseIn*)sData.pHead)->eType= eResponse;
			((SBaseIn*)sData.pHead)->nChan= m_sInitFT.nChan;
			((SBaseIn*)sData.pHead)->nError= nError;
			m_pcComm->SendData(&sData, llId);
		}
	}
}

DWORD CADCPeriph::ThreadProc()
{
	HANDLE ahEvents[]= {m_hThreadClose, m_hProcessData, m_hReset};
	bool bDone= false;
	DWORD dwStart, dwMid, dwEnd;
	DWORD dwInitialRead= 0;

	while (!bDone)
	{
		dwStart= GetTickCount();
		switch (WaitForMultipleObjects(3, ahEvents, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			bDone= true;
			break;
		case WAIT_OBJECT_0 + 1:
			dwInitialRead= m_psDataHeader->dwCount1+m_psDataHeader->dwCount2;
			m_dwAmountRead= 0;
			dwMid= GetTickCount();
			ExtractData();
			dwEnd= GetTickCount();
			m_fTimeWorked= (float)((double)(dwEnd-dwMid)/(dwEnd-dwStart));
			m_fSpaceFull= (float)((double)(m_dwAmountRead+m_psDataHeader->dwCount1+
				m_psDataHeader->dwCount2-dwInitialRead)/(m_sInitFT.dwBuff/
				(2*((m_sInit.ucBitsPerData/8+(m_sInit.bStatusReg?1:0))*m_ucTransPerByte+1))));
			m_fDataRate= (float)((double)(m_dwAmountRead+m_psDataHeader->dwCount1+
				m_psDataHeader->dwCount2-dwInitialRead)/(dwEnd-dwStart)*1000);
			break;
		case WAIT_OBJECT_0 + 2:
			m_psDataHeader->dwCount1= 0;
			m_psDataHeader->dwCount2= 0;
			m_fSpaceFull= 0;
			m_fTimeWorked= 0;
			m_fDataRate= 0;
			m_psDataHeader->ucError= 0;
			m_psDataHeader->sDataBase.nError= 0;
			m_dwDataCount= 0;
			m_usBadRead= 0;
			m_usOverflow= 0;
			m_bSecond= false;
			m_ucLastByte= 0xFF;
			ResetEvent(m_hReset);
			break;
		default:
			break;
		}
	}
	return 0;
}

// this needs to take faster than a single read/write, otherwise we'd lose data
void CADCPeriph::ExtractData()
{
	if (m_bError)
		return;
	EnterCriticalSection(&m_hDataSafe);
	unsigned char* rx= m_psRx->aucBuff-1;	// buffer was allocated with 2 bytes extra before this pointer
	// this will make sure the FTDI thread doesn't start using this buffer
	// it doesn't make sure that we don't miss data if the FTDI thread has flown through these buffers before we start reading
	CRITICAL_SECTION* psSafe= &m_psRx->sSafe;
	m_psDataHeader->dStartTime= m_dTimeS;
	ResetEvent(m_hProcessData);	// got the data
	LeaveCriticalSection(&m_hDataSafe);
	EnterCriticalSection(&m_hStateSafe);
	__int64	llId= m_llId;	// get most recent ID
	LeaveCriticalSection(&m_hStateSafe);

	m_psDataHeader->dwChan1S= m_psDataHeader->dwCount1;
	m_psDataHeader->dwChan2S= m_psDataHeader->dwCount2;
	const unsigned char ucData= 0xFF<<m_sInit.ucLowestDataBit;
	const unsigned char ucShift= 8/m_ucTransPerByte;
	rx[0]= m_ucLastByte;

	EnterCriticalSection(psSafe);
	// start at one since 1 has new clocked in data (read before write and last to writes are same)
	for (DWORD i= 1; i<=m_sInitFT.dwBuff;)
	{
		// check if this is the start data signal
		
		if ((rx[i-1]&(0x80|1<<m_sInit.ucClk)) == 0 && ((rx[i-1]<<1&0x80)|(0x40|1<<m_sInit.ucClk)) == (rx[i]&(0xC0|1<<m_sInit.ucClk)))
		{
			if (m_dwDataCount)	// if we're in the middle of another data segment bad read
				++m_usBadRead;
			m_dwDataCount= 0;
		}
		
		if (m_dwDataCount == 0)	//	need to find data start signal
		{
			if ((rx[i-1]&(0x80|1<<m_sInit.ucClk)) == 0 && ((rx[i-1]<<1&0x80)|(0x40|1<<m_sInit.ucClk)) == (rx[i]&(0xC0|1<<m_sInit.ucClk)))
			{	//found
				if (rx[i]&0x80)			// get overflow flag
					++m_usOverflow;
				++m_dwDataCount;
				m_ucFlags= 0;
				m_dwTempData= 0;
			}
		} else if (m_sInit.bStatusReg && m_dwDataCount <= m_ucTransPerByte)	// read status register
		{
			if ( rx[i]&1<<m_sInit.ucClk && (rx[i]&(ucData|1<<m_sInit.ucClk)) == ((ucData|1<<m_sInit.ucClk)&~(rx[i-1]&(ucData|1<<m_sInit.ucClk))))
			{
				if (m_sInit.bReverseBytes)
					m_ucFlags |= (rx[i-1]&ucData)>>(m_dwDataCount-1)*ucShift;
				else
					m_ucFlags |= (rx[i-1]&ucData)>>(m_ucTransPerByte-m_dwDataCount)*ucShift;
				if (m_dwDataCount++ == m_ucTransPerByte)
				{
					if (m_sInit.bReverseBytes)
						m_ucFlags= BitReverseTable256[m_ucFlags];
					if (m_ucFlags&0xA0 || !(m_ucFlags&0x08))	// make sure the status register matches
					{
						++m_usBadRead;
						m_dwDataCount= 0;
					} else
					{
						m_psDataHeader->ucError |= (m_ucFlags&0x05)<<((m_ucFlags&0x40)?4:0);	// chan1 or 2 error
						m_bSecond= (m_ucFlags&0x40) != 0;
					}
				}
			}
		} else	// either we already read status, or we don't read it
		{
			if ( (rx[i]&1<<m_sInit.ucClk) && (rx[i]&(ucData|1<<m_sInit.ucClk)) == ((ucData|1<<m_sInit.ucClk)&~(rx[i-1]&(ucData|1<<m_sInit.ucClk))))
			{
				if (m_sInit.bReverseBytes)	// read next data seg
					m_dwTempData |= (rx[i-1]&ucData)>>((m_dwDataCount-1)%m_ucTransPerByte)*ucShift;
				else
					m_dwTempData |= (rx[i-1]&ucData)>>((m_ucTransPerByte-1-(m_dwDataCount-1)%m_ucTransPerByte))*ucShift;
				++m_dwDataCount;
				if (m_sInit.bReverseBytes && !((m_dwDataCount-1)%m_ucTransPerByte))	// reverse byte when read full byte
					*(unsigned char*)(&m_dwTempData)= BitReverseTable256[m_dwTempData&0xFF];
				if (!((m_dwDataCount-1)%m_ucTransPerByte) &&	// if finished byte, but not last byte make room for next byte
					((m_dwDataCount-1)/m_ucTransPerByte - (m_sInit.bStatusReg?1:0)) < m_sInit.ucBitsPerData/8U)
					m_dwTempData= m_dwTempData<<8;
				if (!((m_dwDataCount-1)%m_ucTransPerByte) &&	// if finished last byte, send
					((m_dwDataCount-1)/m_ucTransPerByte - (m_sInit.bStatusReg?1:0)) == m_sInit.ucBitsPerData/8)
				{
					if (m_bSecond)// && m_sInit.bChan2)
					{
						if (m_sInit.bChan2)
							m_adwData[m_psDataHeader->dwChan2Start+m_psDataHeader->dwCount2++]= m_dwTempData;
						else
							++m_usBadRead;
					}
					else
						m_adwData[m_psDataHeader->dwCount1++]= m_dwTempData;
					m_dwDataCount= 0;
					// full so send data
					if (m_psDataHeader->dwCount1 == m_sInit.dwDataPerTrans || m_psDataHeader->dwCount2 == m_sInit.dwDataPerTrans)
					{
						m_psDataHeader->sDataBase.nError= m_usBadRead | (m_usOverflow<<16);
						m_psDataHeader->dwPos= m_dwPos++;
						m_dwAmountRead+= m_psDataHeader->dwCount1+m_psDataHeader->dwCount2;
						m_psDataHeader->fSpaceFull= m_fSpaceFull;
						m_psDataHeader->fTimeWorked= m_fTimeWorked;
						m_psDataHeader->fDataRate= m_fDataRate;
						m_sData.pHead= m_psDataHeader;
						m_pcComm->SendData(&m_sData, llId);
						m_psDataHeader= (SADCData*) m_pcMemPool->PoolAcquire(sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*(m_sInit.bChan2?2:1));
						if (!m_psDataHeader)
						{
							m_bError= true;
							LeaveCriticalSection(psSafe);
							return;
						}
						m_psDataHeader->dwChan2Start= m_sInit.dwDataPerTrans;
						m_psDataHeader->dwCount1= 0;
						m_psDataHeader->dwCount2= 0;
						m_psDataHeader->sDataBase.dwSize=sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*(m_sInit.bChan2?2:1);
						m_psDataHeader->sDataBase.eType= eTrigger;
						m_psDataHeader->sDataBase.nChan= m_sInitFT.nChan;
						m_psDataHeader->sBase.dwSize= sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*(m_sInit.bChan2?2:1)-sizeof(SBaseIn);
						m_psDataHeader->sBase.eType= eADCData;
						m_psDataHeader->dStartTime= 0;
						m_psDataHeader->dwChan1S= 0;
						m_psDataHeader->dwChan2S= 0;
						m_adwData= (DWORD*)((unsigned char*)m_psDataHeader+sizeof(SADCData));
						m_psDataHeader->ucError= 0;
						m_psDataHeader->sDataBase.nError= 0;
						m_usBadRead= 0;
						m_usOverflow= 0;
					}
				}
			}
		}
		++i;
	}
	m_ucLastByte= rx[m_sInitFT.dwBuff];
	LeaveCriticalSection(psSafe);
}