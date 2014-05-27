
#include "cpl defs.h"
#include "ftdi device.h"

static const unsigned char s_aucBitReverseTable256[] = 
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
	HANDLE hNewData, int &nError, CTimer* pcTimer):
	CPeriphFTDI(ADC_P, sInitFT), 
	m_sInit(sADCInit), m_ucDefault(1 << sADCInit.ucClk), m_ucMask(~(1 << sADCInit.ucClk|((0xFF << sADCInit.ucLowestDataBit) &
	(0xFF >> (6 - sADCInit.ucLowestDataBit - sADCInit.ucDataBits))))),
	m_ucConfigWriteBit(1 << (sADCInit.bReverseBytes?sADCInit.ucLowestDataBit:sADCInit.ucLowestDataBit+1+sADCInit.ucDataBits)),
	m_ucConfigReadBit(sADCInit.bReverseBytes?(m_ucConfigWriteBit << 1):(m_ucConfigWriteBit >> 1)), m_ucReverse(sADCInit.bReverseBytes?1:0),
	m_usConfigWord((((sADCInit.bChop?0x80:0) | (sADCInit.ucRateFilter & 0x7F)) << 8) | (sADCInit.ucDataBits+1) << 5
	| (sADCInit.ucBitsPerData == 24?0x10:0) | (sADCInit.bChan2?0x8:0) | (sADCInit.bChan1?0x4:0) | (sADCInit.ucInputRange & 0x3)),
	m_ucTransPerByte(0), m_hNext(hNewData),
	m_ucNGroups(((sADCInit.ucDataBits==0) | (sADCInit.ucDataBits==2) | (sADCInit.ucDataBits==6))?(m_sInit.ucBitsPerData/8+1)*8/(sADCInit.ucDataBits+2):8),
	m_ucNBytes(((sADCInit.ucDataBits==0) | (sADCInit.ucDataBits==2) | (sADCInit.ucDataBits==6))?(m_sInit.ucBitsPerData/8 + 1):(sADCInit.ucDataBits + 2)),
	m_hProcessData(CreateEvent(NULL, TRUE, FALSE, NULL)), m_apsReadIdx(m_hProcessData)
{
	nError= 0;
	m_bError= true;
	m_hThreadClose= NULL;
	m_hReset= NULL;
	m_hThread= NULL;
	m_pcMemRing = NULL;
	m_psDataHeader= NULL;
	m_pcMemPool= new CMemPool;
	m_pcTimer= NULL;
	InitializeCriticalSection(&m_hStateSafe);
	// assumption is that the ADC buffer length is the one that makes dwBuff largest, i.e. ADC buffer is always
	// equal to dwBuff
	if (!pcComm || (m_sInitFT.dwBuff%510 && m_sInitFT.dwBuff%62) ||
		(m_sInit.ucBitsPerData != 16 && m_sInit.ucBitsPerData != 24) ||
		(!m_sInit.bStatusReg) || m_sInitFT.dwMinSizeR != m_sInitFT.dwBuff ||
		m_sInitFT.dwMinSizeW != m_sInitFT.dwBuff || !pcTimer || !hNewData || (!m_sInit.bChan1 && !m_sInit.bChan2)
		|| m_sInit.ucDataBits > 6)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_pcComm= pcComm;
	m_pcTimer= pcTimer;
	m_hThreadClose= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hReset= CreateEvent(NULL, TRUE, TRUE, NULL);
	if (!m_hThreadClose || !m_hProcessData || !m_hReset)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}
	m_psDataHeader= (SADCData*) m_pcMemPool->PoolAcquire(sizeof(SADCData)+sizeof(DWORD)*sADCInit.dwDataPerTrans*((sADCInit.bChan1 && sADCInit.bChan2)?2:1));
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
	m_psDataHeader->dwChan2Start = sADCInit.bChan1?sADCInit.dwDataPerTrans:0;
	m_psDataHeader->dwCount1= 0;
	m_psDataHeader->dwCount2= 0;
	m_psDataHeader->dwChan1S= 0;
	m_psDataHeader->dwChan2S= 0;
	m_psDataHeader->sDataBase.dwSize=sizeof(SADCData)+sizeof(DWORD)*sADCInit.dwDataPerTrans*((sADCInit.bChan1 && sADCInit.bChan2)?2:1);
	m_psDataHeader->sDataBase.eType= eTrigger;
	m_psDataHeader->sDataBase.nChan= m_sInitFT.nChan;
	m_psDataHeader->sBase.dwSize= sizeof(SADCData)+sizeof(DWORD)*sADCInit.dwDataPerTrans*((sADCInit.bChan1 && sADCInit.bChan2)?2:1)-sizeof(SBaseIn);
	m_psDataHeader->sBase.eType= eADCData;
	m_psDataHeader->dStartTime= 0;
	m_adwData= (DWORD*)((unsigned char*)m_psDataHeader+sizeof(SADCData));
	m_eConfigState = eConfigDone;
	m_bSecond= false;
	m_llId= -1;	// so that if we send to closed comm unlikly to have this chann open
	m_dwDataCount= 0;
	m_usBadRead= 0;
	m_usOverflow= 0;
	m_bError= false;
	m_eState= eInactivateState;
	m_dwPos= 0;
	m_sData.pDevice= this;
	m_sData.dwSize= sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*((sADCInit.bChan1 && sADCInit.bChan2)?2:1);
	m_aucDecoded = (unsigned short(*)[256])m_pcMemPool->PoolAcquire(sizeof(*m_aucDecoded)*m_ucNGroups);
	unsigned __int64 llTemp;
	unsigned char ucTemp;
	char cShift;
	// we filter out the data ports, align it to the MSB of the 64 bit int and add it
	// into a short which will be or'd with a 64 bit int when reconstructing.
	// m_aucGroups says how far from the start (LSB, 0th byte) of the 8 byte we
	// or the short with. Highest is 6 and it goes down.
	// for negative values we never use i that high anyway when recosntructing. e.g. if ucDataBits + 2 = 8
	// then 6 - 7*8/8 == -1, but when recosntructing max i is 4.
	// Data is sent from ADC MSB first.
	for (char i = 0; i<m_ucNGroups; ++i)
		if (i*(m_sInit.ucDataBits + 2)/8 > m_ucNBytes - 2)
			m_aucGroups[i] = 0;
		else
			m_aucGroups[i] = m_ucNBytes - 2 - i*(m_sInit.ucDataBits + 2)/8;
	for (unsigned short i = 0; i< 256; ++i)
	{
		ucTemp = (i & ~(m_ucMask | 1 << m_sInit.ucClk));
		if (m_sInit.bReverseBytes)
		{
			cShift = m_sInit.ucLowestDataBit - (8 - (m_sInit.ucLowestDataBit + m_sInit.ucDataBits + 2));
			ucTemp = cShift>=0 ? s_aucBitReverseTable256[ucTemp] << cShift: s_aucBitReverseTable256[ucTemp] >> -cShift;
		}
		ucTemp <<= 8 - (m_sInit.ucLowestDataBit + m_sInit.ucDataBits + 2);	// allign data at MSB
		llTemp = (unsigned __int64)ucTemp << 8 * (m_ucNBytes - 1);	// shift it up to allign with MSB of num bytes
		for (char j = 0; j<m_ucNGroups; ++j)
		{
			m_aucDecoded[j][i] = ((llTemp >> m_aucGroups[j] * 8) & 0xFFFF);
			llTemp >>= m_sInit.ucDataBits + 2;
		}
	}
	switch (sADCInit.ucDataBits)
	{
	case 0:
	case 2:
	case 6:
		m_cBuffSize = m_ucNBytes;
		break;
	default:
		m_cBuffSize = m_ucNBytes*(m_sInit.ucBitsPerData/8 + 1);
		break;
	}
	m_aucBuff = (unsigned char*)m_pcMemPool->PoolAcquire(m_cBuffSize);
	memset(m_aucBuff, 0, m_cBuffSize);
	m_cDataOffset = m_ucNBytes * (m_cBuffSize / m_ucNBytes - 1);
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
	DeleteCriticalSection(&m_hStateSafe);
	if (m_hThread) CloseHandle(m_hThread);
	STimedRead *psRead = NULL;
	bool bNotEmpty;
	while (m_pcMemRing && m_apsReadIdx.GetSize())
	{
		psRead = m_apsReadIdx.Front(true, bNotEmpty);
		if (psRead && bNotEmpty)
		{
			m_pcMemRing->ReleaseIndex(psRead->nIdx);
			delete psRead;
		}
	}
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
		if (m_sInit.bConfigureADC)
			m_ucBitOutput = m_sInitFT.ucBitOutput | m_ucConfigWriteBit;
		m_eConfigState = eConfigStart;
		break;
	case eInactivateState:
		SData sData;
		sData.dwSize= sizeof(SBaseIn);
		sData.pDevice= this;
		sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseIn));
		if (sData.pHead)
		{
			((SBaseIn*)sData.pHead)->eType= eResponse;
			((SBaseIn*)sData.pHead)->nChan= m_sInitFT.nChan;
			((SBaseIn*)sData.pHead)->nError= DEVICE_CLOSING;
			((SBaseIn*)sData.pHead)->dwSize= sizeof(SBaseIn);
		}
		EnterCriticalSection(&m_hStateSafe);
		m_eState= eInactivateState;
		if (sData.pHead)
			m_pcComm->SendData(&sData, m_llId);
		m_llId= -1;		// unlikly to have comm with this ID
		LeaveCriticalSection(&m_hStateSafe);
		break;
	case eRecover:
		EnterCriticalSection(&m_hStateSafe);
		if (m_eState == eActive || m_eState == eActivateState)	// start over again
		{
			m_eState= eActivateState;
			m_eConfigState = eConfigStart;
			m_ucBitOutput = m_sInitFT.ucBitOutput;
		}
		LeaveCriticalSection(&m_hStateSafe);
		break;
	case ePreWrite:
		if (m_eState == eActivateState)
		{
			switch (m_eConfigState)
			{
			case eConfigStart:
				{
				SetEvent(m_hReset);
				m_dwRestartE= GetTickCount() + ADC_RESET_DELAY;	// time when device should finish reseting
				// the first time we clock out initialization seq so it can reset
				DWORD i = 0;
				for (; i<90; ++i)
				{
					aucBuff[i++] = (aucBuff[i] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
					aucBuff[i] = (aucBuff[i] & m_ucMask) | m_ucConfigWriteBit;
				}
				for (; i<m_sInitFT.dwBuff;++i)
					aucBuff[i]= (aucBuff[i]&m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
				break;
				}
			case eConfigWrite:
				{
				DWORD i = 0;
				for (; i<90; ++i)	// initialization seq, (keep in mind min packet is also 3*62)
				{
					aucBuff[i++] = (aucBuff[i] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
					aucBuff[i] = (aucBuff[i] & m_ucMask) | m_ucConfigWriteBit;
				}
				aucBuff[90] = (aucBuff[90] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
				aucBuff[91] = (aucBuff[91] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
				for (DWORD i = 0; i<10; ++i)	
				{
					aucBuff[2*i + 92] = (aucBuff[2*i + 92] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
					aucBuff[2*i + 1 + 92] = (aucBuff[2*i + 1 + 92] & m_ucMask) | m_ucDefault;
				}
				aucBuff[112] = (aucBuff[112] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
				aucBuff[113] = (aucBuff[113] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
				char value;
				for (DWORD i= 0; i<16; ++i)	
				{
					value = (m_usConfigWord & (1 << (15-i)))?m_ucConfigWriteBit:0;
					aucBuff[3*i+114] = (aucBuff[3*i+114] & m_ucMask) | m_ucDefault | value;
					aucBuff[3*i+1+114] = (aucBuff[3*i+1+114] & m_ucMask) | value;
					aucBuff[3*i+2+114] = (aucBuff[3*i+2+114] & m_ucMask) | m_ucDefault | value;
				}
				break;
				}
			}
		} else if (m_eState == eInactivateState)
		{
			m_ucBitOutput = m_sInitFT.ucBitOutput;
			m_eConfigState = eConfigDone;
			SetEvent(m_hReset);
			for (DWORD i= 0; i<m_sInitFT.dwBuff;++i)
				aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
			EnterCriticalSection(&m_hStateSafe);
			m_eState = eInactive;
			ResetEvent(m_hNext);	// stop R/W
			LeaveCriticalSection(&m_hStateSafe);
		} else  if (m_eState == eActive)
		{
			m_dTimeTemp = g_cTimer.Seconds();	// get start time when we're about to read
		}
		break;
	case ePostWrite:
		if (m_eState == eActivateState)
		{
			if (m_eConfigState == eConfigStart)
			{
				for (int i = 0; i<90; ++i)
					aucBuff[i] = (aucBuff[i] & m_ucMask) | m_ucDefault | m_ucConfigWriteBit;
				m_eConfigState = eConfigTO;
			} else if (m_eConfigState == eConfigDone)
			{
				for (DWORD i= 0; i < m_sInitFT.dwBuff;++i)
				{
					aucBuff[i++] = (aucBuff[i] & m_ucMask) | m_ucDefault;
					aucBuff[i] = aucBuff[i] & m_ucMask;
				}
				aucBuff[m_sInitFT.dwBuff-1] = (aucBuff[m_sInitFT.dwBuff-1] & m_ucMask) | m_ucDefault;
			}
		}
		break;
	case ePostRead:
		if (m_eState == eActivateState)
		{
			switch (m_eConfigState)
			{
			case eConfigTO:
				{
				if (m_dwRestartE <= GetTickCount())
				{
					if (!m_sInit.bConfigureADC)
						m_eConfigState = eConfigDone;
					else
						m_eConfigState = eConfigWrite;
				}
				break;
				}
			case eConfigWrite:
				{
				unsigned short usConfigWord = 0;
				unsigned char * aucRx = (unsigned char *)m_pcMemRing->GetIndexMemoryUnsafe(*(int *)pHead);
				for (char i = 0; i < 16; ++i)
					if (aucRx[3*i + 116] & m_ucConfigReadBit)
						usConfigWord |= 1 << (15 - i);
				if (usConfigWord != m_usConfigWord)
					m_eConfigState = eConfigStart;
				else
					m_eConfigState = eConfigDone;
				break;
				}
			case eConfigDone:
				{
				m_dwPos = 0;
				m_ucBitOutput = m_sInitFT.ucBitOutput;
				m_cDataOffset = m_ucNBytes * (m_cBuffSize / m_ucNBytes - 1);
				m_dwDataCount = 0;
				m_dwSpaceUsed = 0;
				m_dwStartRead = 0;
				m_dwStartRead = GetTickCount();
				EnterCriticalSection(&m_hStateSafe);
				m_eState = eActive;
				LeaveCriticalSection(&m_hStateSafe);
				break;
				}
			}
		} else if (m_eState == eActive)	// select which read buffer to use
		{
			STimedRead sTimed;
			sTimed.nIdx = *(int *)pHead;
			sTimed.dTime = m_dTimeTemp;
			sTimed.pHead = m_pcMemRing->GetIndexMemory(sTimed.nIdx);
			m_apsReadIdx.Push(new STimedRead(sTimed));
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
	bool bDone= false, bNotEmpty;
	DWORD dwStart, dwMid, dwEnd;
	DWORD dwInitialRead= 0;
	STimedRead *psRead = NULL;

	while (!bDone)
	{
		dwStart= GetTickCount();
		switch (WaitForMultipleObjects(3, ahEvents, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			bDone= true;
			break;
		case WAIT_OBJECT_0 + 1:
			psRead = m_apsReadIdx.Front(true, bNotEmpty);
			if (psRead && bNotEmpty)
			{
				dwInitialRead= m_psDataHeader->dwCount1+m_psDataHeader->dwCount2;
				m_dwAmountRead= 0;
				dwMid= GetTickCount();
				ExtractData(psRead);
				dwEnd= GetTickCount();
				m_dwAmountRead += m_psDataHeader->dwCount1+m_psDataHeader->dwCount2;
				m_fTimeWorked= (float)((double)(dwEnd-dwMid)/(dwEnd-dwStart));
				//m_fSpaceFull= (float)((double)(m_dwAmountRead-dwInitialRead)/(m_sInitFT.dwBuff/
				//	(2*((m_sInit.ucBitsPerData/8+(m_sInit.bStatusReg?1:0))*m_ucTransPerByte+1))));
				//m_fDataRate= (float)((double)(m_dwAmountRead-dwInitialRead)/(dwEnd-dwStart)*1000/((m_sInit.bChan1 && m_sInit.bChan2)?2:1));
				m_pcMemRing->ReleaseIndex(psRead->nIdx);
				delete psRead;
			}
			m_apsReadIdx.ResetIfEmpty();
			break;
		case WAIT_OBJECT_0 + 2:
			m_psDataHeader->dwCount1= 0;
			m_psDataHeader->dwCount2= 0;
			m_dwSpaceUsed = 0;
			m_fTimeWorked= 0;
			//m_fDataRate= 0;
			m_psDataHeader->ucError= 0;
			m_psDataHeader->sDataBase.nError= 0;
			m_dwDataCount= 0;
			m_usBadRead= 0;
			m_usOverflow= 0;
			m_bSecond= false;
			//m_ucLastByte= 0xFF;
			ResetEvent(m_hReset);
			break;
		default:
			break;
		}
	}
	return 0;
}

// this needs to take faster than a single read/write, otherwise we'd lose data
void CADCPeriph::ExtractData(STimedRead *psRead)
{
	if (m_bError)
		return;
	unsigned char* rx = (unsigned char *)psRead->pHead;
	m_psDataHeader->dStartTime = psRead->dTime;
	EnterCriticalSection(&m_hStateSafe);
	__int64	llId = m_llId;	// get most recent ID
	LeaveCriticalSection(&m_hStateSafe);

	m_psDataHeader->dwChan1S = m_psDataHeader->dwCount1;
	m_psDataHeader->dwChan2S = m_psDataHeader->dwCount2;
	unsigned char ucTemp1, ucTemp2;

	// start at one since 1 has new clocked in data (read before write and last to writes are same)
	for (DWORD i= 0; i<=m_sInitFT.dwBuff-1;++i)
	{
		++m_dwSpaceUsed;
		// check if this is the start data signal
		if (!(rx[i]&1<<m_sInit.ucClk) && (unsigned char)(rx[i]&~(m_ucMask|m_ucConfigWriteBit)) == (unsigned char)(~rx[i+1]&~(m_ucMask|m_ucConfigWriteBit))
			&& (rx[i] & m_ucConfigWriteBit) == (rx[i+1] & m_ucConfigWriteBit))
		{
			if (m_dwDataCount)	// if we're in the middle of another data segment bad read
			{
				++m_usBadRead;
				m_dwDataCount = 0;
			} else
			{
				memset(m_aucBuff+m_cDataOffset, 0, m_ucNBytes);
				m_dwDataCount= 1;
				*(unsigned short*)(m_aucBuff+m_aucGroups[0]+m_cDataOffset) = m_aucDecoded[0][rx[i]];
			}
			continue;
		}
		ucTemp1 = rx[i]&~m_ucMask;
		ucTemp2 = rx[i+1]&~m_ucMask;
		// if data was not changed or clock is high
		if ((ucTemp1&~(1<<m_sInit.ucClk)) == (ucTemp2&~(1<<m_sInit.ucClk)) || ucTemp1&(1<<m_sInit.ucClk))
			continue;
		// haven't hit start or they are not complemented
		if (!m_dwDataCount || ucTemp1 != (unsigned char)((~ucTemp2)&(~m_ucMask)))
		{
			++m_usBadRead;
			m_dwDataCount = 0;
			continue;
		}
		*(unsigned short*)(m_aucBuff+m_aucGroups[m_dwDataCount] + m_cDataOffset) |= m_aucDecoded[m_dwDataCount][rx[i]];
		++m_dwDataCount;

		if (m_dwDataCount == m_ucNGroups)
		{
			m_dwDataCount = 0;
			m_cDataOffset -= m_ucNBytes;
		}
#pragma NOTE("m_aucBuff only works on little endian systems.")
		const char cADCSize = m_sInit.ucBitsPerData/8 + 1;
		unsigned char ucStatusReg;
		DWORD dwDataPoint;
		if (m_cDataOffset == -m_ucNBytes)	// we have a multiple of a full transection set
		{
			m_cDataOffset = m_ucNBytes * (m_cBuffSize / m_ucNBytes - 1);
			for (char j = m_cBuffSize/cADCSize - 1; j >= 0; --j)
			{
				// full so send data
				if (m_psDataHeader->dwCount1 == m_sInit.dwDataPerTrans || m_psDataHeader->dwCount2 == m_sInit.dwDataPerTrans)
				{
					m_psDataHeader->sDataBase.nError = m_usBadRead | (m_usOverflow<<16);
					m_psDataHeader->dwPos= m_dwPos++;
					m_dwAmountRead += m_psDataHeader->dwCount1+m_psDataHeader->dwCount2;
					if (m_dwSpaceUsed)
						m_psDataHeader->fSpaceFull = (float)((m_psDataHeader->dwCount1 + m_psDataHeader->dwCount2) * cADCSize * 8 / (double)((m_sInit.ucDataBits + 2) * m_dwSpaceUsed));
					else
						m_psDataHeader->fSpaceFull = 0.0;
					m_dwSpaceUsed = 0;
					m_psDataHeader->fTimeWorked = m_fTimeWorked;
					m_psDataHeader->fDataRate = (float)((m_psDataHeader->dwCount1 + m_psDataHeader->dwCount2) / (double)(GetTickCount() - m_dwStartRead) * 1000);
					m_psDataHeader->fDataRate /= ((m_sInit.bChan1 && m_sInit.bChan2)?2:1);
					m_dwStartRead = GetTickCount();
					m_sData.pHead = m_psDataHeader;
					m_pcComm->SendData(&m_sData, llId);
					m_psDataHeader = (SADCData*) m_pcMemPool->PoolAcquire(sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*((m_sInit.bChan1 && m_sInit.bChan2)?2:1));
					if (!m_psDataHeader)
					{
						m_bError = true;
						return;
					}
					m_psDataHeader->dwChan2Start = m_sInit.bChan1?m_sInit.dwDataPerTrans:0;
					m_psDataHeader->dwCount1 = 0;
					m_psDataHeader->dwCount2 = 0;
					m_psDataHeader->sDataBase.dwSize =sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*((m_sInit.bChan1 && m_sInit.bChan2)?2:1);
					m_psDataHeader->sDataBase.eType = eTrigger;
					m_psDataHeader->sDataBase.nChan = m_sInitFT.nChan;
					m_psDataHeader->sBase.dwSize = sizeof(SADCData)+sizeof(DWORD)*m_sInit.dwDataPerTrans*((m_sInit.bChan1 && m_sInit.bChan2)?2:1)-sizeof(SBaseIn);
					m_psDataHeader->sBase.eType = eADCData;
					m_psDataHeader->dStartTime = 0;
					m_psDataHeader->dwChan1S = 0;
					m_psDataHeader->dwChan2S = 0;
					m_adwData = (DWORD*)((unsigned char*)m_psDataHeader+sizeof(SADCData));
					memset(m_adwData, 0, sizeof(DWORD)*m_sInit.dwDataPerTrans*((m_sInit.bChan1 && m_sInit.bChan2)?2:1));
					m_psDataHeader->ucError = 0;
					m_psDataHeader->sDataBase.nError = 0;
					m_usBadRead = 0;
					m_usOverflow = 0;
				}
				ucStatusReg = m_aucBuff[j * cADCSize + cADCSize - 1];
				m_bSecond = (ucStatusReg & 0x40) != 0;
				if (ucStatusReg & 0x80)			// get overflow flag
					++m_usOverflow;
				dwDataPoint = 0;
				memcpy(&dwDataPoint, m_aucBuff + j * cADCSize, cADCSize - 1);
				if (ucStatusReg & 0x20 || !(ucStatusReg & 0x08))	// make sure the status register matches
				{
					++m_usBadRead;
					continue;
				} else
					m_psDataHeader->ucError |= (ucStatusReg & 0x05) << (m_bSecond?4:0);	// chan1 or 2 error
				if (m_bSecond)
				{
					if (m_sInit.bChan2)
						m_adwData[m_psDataHeader->dwChan2Start + m_psDataHeader->dwCount2++] = dwDataPoint;
					else 
					{
						++m_usBadRead;
						continue;
					}
				} else
				{
					if (m_sInit.bChan1)
						m_adwData[m_psDataHeader->dwCount1++]= dwDataPoint;
					else 
					{
						++m_usBadRead;
						continue;
					}

				}
			}
		}
	}
}