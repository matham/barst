#include "cpl defs.h"
#include "ftdi device.h"





CMultiWPeriph::CMultiWPeriph(const SValveInit &sValveInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
	int &nError, HANDLE hNewData, CTimer* pcTimer) : CPeriphFTDI(MULTI_W_P, sInitFT), m_sInit(sValveInit),
	m_abData(sValveInit.dwBoards*8, false), m_ucDefault(1<<sValveInit.ucLatch), m_ucMask(~(1<<sValveInit.ucLatch|
	1<<sValveInit.ucClk|1<<sValveInit.ucData)), m_allIds(hNewData)
{
	nError= 0;
	m_bError= true;
	m_pcMemPool= new CMemPool;
	InitializeCriticalSection(&m_hDataSafe);
	// minw has to be at least as large as required for writing full boards, dwbuff also has to be as large, but can be larger
	// e.g. if adc device is also active this dwBuff will be larger than dwMinSizeW
	if (!pcComm || sInitFT.dwMinSizeW < (sValveInit.dwBoards*8*2+2)*sValveInit.dwClkPerData ||
		sInitFT.dwBuff < sInitFT.dwMinSizeW || !sValveInit.dwClkPerData || !sValveInit.dwBoards || !pcTimer ||
		!hNewData)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_pcComm= pcComm;
	m_pcTimer= pcTimer;
	m_bUpdated= false;
	m_bChanged= false;
	m_nProcessed= 0;
	m_bError= false;
	m_eState= eInactivateState;
	m_hNext= hNewData;	// you set this when new data is availible to make channel go
}

DWORD CMultiWPeriph::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT);
	if (dwSize<sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT);
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

	((SBase*)pHead)->dwSize= sizeof(SValveInit)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIMultiWriteInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sInit, sizeof(SValveInit));

	return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT);
}

CMultiWPeriph::~CMultiWPeriph()
{
	DeleteCriticalSection(&m_hDataSafe);
	delete m_pcMemPool;
}

bool CMultiWPeriph::DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError)
{
	if (m_bError)
		return true;
	unsigned char *aucBuff= (unsigned char *)pHead;
	bool bNotEmpty;

	if (eReason == eActivateState || eReason == eInactivateState)	// update state
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eReason == eActivateState?eActive:eInactivateState;
		LeaveCriticalSection(&m_hDataSafe);
	} else if (eReason == eRecover)
	{
		EnterCriticalSection(&m_hDataSafe);
		if (m_eState == eActive || m_eState == eInactivateState)
		{
			m_bUpdated= true;		// clock out last data again
			SetEvent(m_hNext);
		}
		LeaveCriticalSection(&m_hDataSafe);
	}

	if (m_eState == eInactivateState && eReason == ePreWrite)	// shut it down
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eInactive;
		m_bUpdated= false;
		LeaveCriticalSection(&m_hDataSafe);
		ResetEvent(m_hNext);
		for (int i= 0; i<m_allIds.GetSize(); ++i)	// respond to all waiting users
		{
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
				m_pcComm->SendData(&sData, m_allIds.Front(true, bNotEmpty));
			}
		}
		m_nProcessed= 0;
		m_bChanged= false;
		// write full buffer length (even more than dwMinSizeW)
		for (DWORD i= 0; i<m_sInitFT.dwBuff;++i)				// now set these pins to default
			aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
	} else if (m_eState == eActive && eReason == ePreWrite)	// write the next set of data
	{
		EnterCriticalSection(&m_hDataSafe);
		if (m_bUpdated)
		{
			int k= 0;
			DWORD i= 0;
			for (;i<m_sInit.dwBoards*8*2*m_sInit.dwClkPerData;)
			{
				// count number of boards from closest to computer to furthest, bytes that are written first control furthest board
				// so write last board at the start of the buffer and the first board at end of the buffer.
				for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
					aucBuff[i++]= (aucBuff[i]&m_ucMask)|(m_abData[(m_sInit.dwBoards-1-k/8)*8+k%8]?1<<m_sInit.ucData:0)|1<<m_sInit.ucClk;
				for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
					aucBuff[i++]= (aucBuff[i]&m_ucMask)|(m_abData[(m_sInit.dwBoards-1-k/8)*8+k%8]?1<<m_sInit.ucData:0);
				++k;
			}
			for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
				aucBuff[i]= aucBuff[i++]&m_ucMask;
			for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
				aucBuff[i]= (aucBuff[i++]&m_ucMask)|1<<m_sInit.ucLatch;
			m_bUpdated= false;	// done updating
			m_bChanged= true;
			m_nProcessed= m_allIds.GetSize();	// current number of updates proccesed
			m_dInitial= m_pcTimer->Seconds();	// time right before writing
			ResetEvent(m_hNext);
		}
		LeaveCriticalSection(&m_hDataSafe);
	} else if (m_eState == eActive && eReason == ePostWrite) // now let user know if succesful and set buffer to default.
	{
		if (m_bChanged)
		{
			for (int i= 0; i<m_nProcessed; ++i)
			{
				SData sData;
				sData.dwSize= sizeof(SBaseOut);
				sData.pDevice= this;
				sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseOut));
				if (sData.pHead)
				{
					((SBaseOut*)sData.pHead)->sBaseIn.eType= eResponseExD;
					((SBaseOut*)sData.pHead)->sBaseIn.nChan= m_sInitFT.nChan;
					((SBaseOut*)sData.pHead)->sBaseIn.nError= nError;
					((SBaseOut*)sData.pHead)->sBaseIn.dwSize= sizeof(SBaseOut);
					((SBaseOut*)sData.pHead)->dDouble= m_dInitial;
					m_pcComm->SendData(&sData, m_allIds.Front(true, bNotEmpty));
				}
			}
			m_nProcessed= 0;
			for (DWORD i= 0; i<(m_sInit.dwBoards*8*2+2)*m_sInit.dwClkPerData;++i)
				aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
			m_bChanged= false;
		}
	}
	return true;
}

EStateFTDI CMultiWPeriph::GetState()
{
	EnterCriticalSection(&m_hDataSafe);
	EStateFTDI eState= m_eState;
	LeaveCriticalSection(&m_hDataSafe);
	return eState;
}

void CMultiWPeriph::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if(m_bError)
		return;
	if (!pHead || dwSize <= sizeof(SBaseIn)+sizeof(SBase) || ((SBaseIn*)pHead)->eType != eData 
		|| ((SBase*)((char*)pHead+sizeof(SBaseIn)))->eType != eFTDIMultiWriteData ||
		((SBaseIn*)pHead)->dwSize != dwSize || (dwSize-sizeof(SBaseIn)-sizeof(SBase))%sizeof(SValveData))	// there has to be at least some data
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

	SValveData *asValveData= (SValveData*)((char*)pHead + sizeof(SBaseIn) + sizeof(SBase));
	int nError= 0;
	EnterCriticalSection(&m_hDataSafe);
	if (m_eState == eActive)
	{
		for (DWORD i= 0; i<(dwSize-sizeof(SBaseIn)-sizeof(SBase))/sizeof(SValveData); ++i)
		{
			if (asValveData[i].usIndex<m_abData.size())
				m_abData[asValveData[i].usIndex]= asValveData[i].bValue;
			else
				nError= BAD_INPUT_PARAMS;
		}
		if (!nError)
		{
			m_bUpdated= true;
			m_allIds.Push(llId);	// pipe to notify of success
			SetEvent(m_hNext);		// update only in cc
		}
	} else
		nError= INACTIVE_DEVICE;
	LeaveCriticalSection(&m_hDataSafe);

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



CMultiRPeriph::CMultiRPeriph(const SValveInit &sValveInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
	int &nError, HANDLE hNewData, CTimer* pcTimer) : CPeriphFTDI(MULTI_R_P, sInitFT), m_sInit(sValveInit),
	m_ucDefault(1<<sValveInit.ucLatch|1<<sValveInit.ucClk), m_ucMask(~(1<<sValveInit.ucLatch|
	1<<sValveInit.ucClk|1<<sValveInit.ucData)), m_allIds(hNewData)
{
	nError= 0;
	m_bError= true;
	InitializeCriticalSection(&m_hDataSafe);
	m_pcMemPool= new CMemPool;
	// minw has to be at least as large as required for writing full boards, dwbuff also has to be as large
	if (!pcComm || sInitFT.dwMinSizeW < (sValveInit.dwBoards*8*2+2+4)*sValveInit.dwClkPerData ||
		m_sInitFT.dwBuff < sInitFT.dwMinSizeW || sInitFT.dwMinSizeW != sInitFT.dwMinSizeR || 
		!sValveInit.dwClkPerData || !sValveInit.dwBoards || !pcTimer || !hNewData)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_pcComm= pcComm;
	m_pcTimer= pcTimer;
	m_bRead= false;
	m_nProcessed= 0;
	m_bError= false;
	m_eState= eInactivateState;
	m_hNext= hNewData;	// you set this when new data is availible
}

DWORD CMultiRPeriph::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT);
	if (dwSize<sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT);
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

	((SBase*)pHead)->dwSize= sizeof(SValveInit)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIMultiReadInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sInit, sizeof(SValveInit));

	return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SInitPeriphFT);
}

CMultiRPeriph::~CMultiRPeriph()
{
	DeleteCriticalSection(&m_hDataSafe);
	delete m_pcMemPool;
}

bool CMultiRPeriph::DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError)
{
	if (m_bError)
		return true;
	unsigned char *aucBuff= (unsigned char *)pHead;
	bool bNotEmpty;

	if (eReason == eActivateState || eReason == eInactivateState)	// update state
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eReason == eActivateState?eActive:eInactivateState;
		LeaveCriticalSection(&m_hDataSafe);
	}

	if (m_eState == eInactivateState && eReason == ePreWrite)	// shut it down
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eInactive;
		LeaveCriticalSection(&m_hDataSafe);
		ResetEvent(m_hNext);
		for (int i= 0; i<m_allIds.GetSize(); ++i)	// respond to all waiting users
		{
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
				m_pcComm->SendData(&sData, m_allIds.Front(true, bNotEmpty));
			}
		}
		m_nProcessed= 0;
		m_bRead= false;
		// do this for the full beffer length
		for (DWORD i= 0; i<m_sInitFT.dwBuff;++i)				// now set these pins to default
			aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
	} else if (m_eState == eActive && eReason == ePreWrite)	// write the next set of data
	{
		m_nProcessed= m_allIds.GetSize();	// current number of triggers proccesed, or only one if cont
		if (m_nProcessed)
		{
			//int k= 0;
			DWORD i= 0;
			for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
				aucBuff[i++]= (aucBuff[i]&m_ucMask)|1<<m_sInit.ucClk;
			for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
				aucBuff[i++]= (aucBuff[i]&m_ucMask)|(1<<m_sInit.ucLatch|1<<m_sInit.ucClk);
			for (;i<(4+m_sInit.dwBoards*8*2)*m_sInit.dwClkPerData;)
			{
				for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
					aucBuff[i++]= (aucBuff[i]&m_ucMask)|1<<m_sInit.ucLatch;
				for (DWORD j= 0; j<m_sInit.dwClkPerData; ++j)
					aucBuff[i++]= (aucBuff[i]&m_ucMask)|(1<<m_sInit.ucLatch|1<<m_sInit.ucClk);
				//++k;
			}
			m_bRead= true;
			m_dInitial= m_pcTimer->Seconds();	// time right before reading
		}
	} else if (m_eState == eActive && eReason == ePostWrite) // let user know if error and set buffer to default.
	{
		if (m_bRead)
		{
			if (nError)
			{
				for (int i= 0; i<m_nProcessed; ++i)	// if error, notfiy here
				{
					SData sData;
					sData.dwSize= sizeof(SBaseOut);
					sData.pDevice= this;
					sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseOut));
					if (sData.pHead)
					{
						((SBaseOut*)sData.pHead)->sBaseIn.eType= eResponseExD;
						((SBaseOut*)sData.pHead)->sBaseIn.nChan= m_sInitFT.nChan;
						((SBaseOut*)sData.pHead)->sBaseIn.nError= nError;
						((SBaseOut*)sData.pHead)->sBaseIn.dwSize= sizeof(SBaseOut);
						((SBaseOut*)sData.pHead)->dDouble= m_dInitial;
						m_pcComm->SendData(&sData, m_allIds.Front(!m_sInit.bContinuous, bNotEmpty));	// if cont, than only one user, don't pop
					}
				}
				m_bRead= false;
				m_nProcessed= 0;
				EnterCriticalSection(&m_hDataSafe);
				if (!m_allIds.GetSize())
					ResetEvent(m_hNext);
				LeaveCriticalSection(&m_hDataSafe);
			}
			for (DWORD i= 0; i<(m_sInit.dwBoards*8*2+2+4)*m_sInit.dwClkPerData;++i)
				aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucDefault;
		}
	} else if (m_eState == eActive && eReason == ePostRead) // let user know if error and set buffer to default.
	{
		if (m_bRead)
		{
			for (int i= 0; i<m_nProcessed; ++i)	// notify users
			{
				SData sData;
				sData.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(bool)*m_sInit.dwBoards*8;
				sData.pDevice= this;
				sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
				if (sData.pHead)
				{
					((SBaseOut*)sData.pHead)->sBaseIn.eType= eResponseExD;
					((SBaseOut*)sData.pHead)->sBaseIn.nChan= m_sInitFT.nChan;
					((SBaseOut*)sData.pHead)->sBaseIn.nError= nError;
					((SBaseOut*)sData.pHead)->sBaseIn.dwSize= sData.dwSize;
					((SBaseOut*)sData.pHead)->dDouble= m_dInitial;
					((SBase*)((char*)sData.pHead+sizeof(SBaseOut)))->eType= eFTDIMultiReadData;
					((SBase*)((char*)sData.pHead+sizeof(SBaseOut)))->dwSize= sizeof(SBase)+sizeof(bool)*m_sInit.dwBoards*8;
					bool* bVal= (bool*)((char*)sData.pHead+sizeof(SBaseOut)+sizeof(SBase));
					for (DWORD i= 0; i<m_sInit.dwBoards*8; ++i)
						bVal[i]= (((SFTBufferSafe*)pHead)->aucBuff[(3+i*2)*m_sInit.dwClkPerData]&1<<m_sInit.ucData) != 0;
					m_pcComm->SendData(&sData, m_allIds.Front(!m_sInit.bContinuous, bNotEmpty));	// if cont, than only one user, don't pop
				}
			}
			m_bRead= false;
			m_nProcessed= 0;
			EnterCriticalSection(&m_hDataSafe);
			if (!m_allIds.GetSize())
				ResetEvent(m_hNext);
			LeaveCriticalSection(&m_hDataSafe);
		}
	}
	return true;
}

EStateFTDI CMultiRPeriph::GetState()
{
	EnterCriticalSection(&m_hDataSafe);
	EStateFTDI eState= m_eState;
	LeaveCriticalSection(&m_hDataSafe);
	return eState;
}

void CMultiRPeriph::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if(m_bError)
		return;
	// only trigger and only accept a trigger for cont if it's the first
	if (!pHead || dwSize != sizeof(SBaseIn) || ((SBaseIn*)pHead)->eType != eTrigger ||
		((SBaseIn*)pHead)->dwSize != dwSize || (m_sInit.bContinuous && m_allIds.GetSize())) 
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
	EnterCriticalSection(&m_hDataSafe);
	if (m_eState == eActive)
	{
		m_allIds.Push(llId);	// pipe to send reads
		SetEvent(m_hNext);
	} else
		nError= INACTIVE_DEVICE;
	LeaveCriticalSection(&m_hDataSafe);

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



CPinWPeriph::CPinWPeriph(const SPinInit &sPinInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
	int &nError, HANDLE hNewData, CTimer* pcTimer) : CPeriphFTDI(PIN_W_P, sInitFT), m_sInit(sPinInit),
	m_aucData(sPinInit.usBytesUsed, sPinInit.ucInitialVal&sPinInit.ucActivePins), m_ucMask(~sPinInit.ucActivePins), 
	m_ucLast(sPinInit.ucInitialVal&sPinInit.ucActivePins), m_allIds(hNewData)
{
	nError= 0;
	m_bError= true;
	InitializeCriticalSection(&m_hDataSafe);
	m_pcMemPool= new CMemPool;
	// minw has to be at least as large as required for writing full boards, dwbuff also has to be as large
	if (!pcComm || sInitFT.dwMinSizeW < sPinInit.usBytesUsed || m_sInitFT.dwBuff < sInitFT.dwMinSizeW 
		|| !sPinInit.usBytesUsed || !sPinInit.ucActivePins || !pcTimer || !hNewData)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_pcComm= pcComm;
	m_pcTimer= pcTimer;
	m_bUpdated= false;
	m_bChanged= false;
	m_nProcessed= 0;
	m_bError= false;
	m_eState= eInactivateState;
	m_hNext= hNewData;	// you set this when new data is availible to make channel go
}

DWORD CPinWPeriph::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT);
	if (dwSize<sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT);
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

	((SBase*)pHead)->dwSize= sizeof(SPinInit)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIPinWriteInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sInit, sizeof(SPinInit));

	return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT);
}

CPinWPeriph::~CPinWPeriph()
{
	DeleteCriticalSection(&m_hDataSafe);
	delete m_pcMemPool;
}

bool CPinWPeriph::DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError)
{
	if (m_bError)
		return true;
	unsigned char *aucBuff= (unsigned char *)pHead;
	bool bNotEmpty;

	if (eReason == eActivateState || eReason == eInactivateState)	// update state
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eReason == eActivateState?eActive:eInactivateState;
		LeaveCriticalSection(&m_hDataSafe);
	} else if (eReason == eRecover)
	{
		EnterCriticalSection(&m_hDataSafe);
		if (m_eState == eActive || m_eState == eInactivateState)
		{
			m_bUpdated= true;		// clock out last data again
			SetEvent(m_hNext);
		}
		LeaveCriticalSection(&m_hDataSafe);
	}

	if (m_eState == eInactivateState && eReason == ePreWrite)	// shut it down
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eInactive;
		m_bUpdated= false;
		LeaveCriticalSection(&m_hDataSafe);
		ResetEvent(m_hNext);
		for (int i= 0; i<m_allIds.GetSize(); ++i)	// respond to all waiting users
		{
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
				m_pcComm->SendData(&sData, m_allIds.Front(true, bNotEmpty));
			}
		}
		m_nProcessed= 0;
		m_bChanged= false;
		// update full buffer
		for (DWORD i= 0; i<m_sInitFT.dwBuff;++i)				// now set these pins to last value
			aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucLast;
	} else if (m_eState == eActive && eReason == ePreWrite)	// write the next set of data
	{
		EnterCriticalSection(&m_hDataSafe);
		if (m_bUpdated)
		{
			unsigned short i= 0;				// write data from buffer
			m_ucLast= m_aucData[m_sInit.usBytesUsed-1];
			for (; i<m_sInit.usBytesUsed; ++i)
			{
				aucBuff[i]= (aucBuff[i]&m_ucMask)|m_aucData[i];
				m_aucData[i]= m_ucLast;
			}
			for (; i<m_sInitFT.dwBuff; ++i)	// write last data until end of buffer
				aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucLast;
			m_bUpdated= false;	// done updating
			m_bChanged= true;
			m_nProcessed= m_allIds.GetSize();	// current number of updates proccesed
			m_dInitial= m_pcTimer->Seconds();	// time right before writing
			ResetEvent(m_hNext);
		}
		LeaveCriticalSection(&m_hDataSafe);
	} else if (m_eState == eActive && eReason == ePostWrite) // now let user know if succesful and set buffer to default.
	{
		if (m_bChanged)
		{
			for (int i= 0; i<m_nProcessed; ++i)
			{
				SData sData;
				sData.dwSize= sizeof(SBaseOut);
				sData.pDevice= this;
				sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseOut));
				if (sData.pHead)
				{
					((SBaseOut*)sData.pHead)->sBaseIn.eType= eResponseExD;
					((SBaseOut*)sData.pHead)->sBaseIn.nChan= m_sInitFT.nChan;
					((SBaseOut*)sData.pHead)->sBaseIn.nError= nError;
					((SBaseOut*)sData.pHead)->sBaseIn.dwSize= sizeof(SBaseOut);
					((SBaseOut*)sData.pHead)->dDouble= m_dInitial;
					m_pcComm->SendData(&sData, m_allIds.Front(true, bNotEmpty));
				}
			}
			m_nProcessed= 0;
			for (unsigned short i= 0; i<m_sInit.usBytesUsed;++i)
				aucBuff[i]= (aucBuff[i]&m_ucMask)|m_ucLast;
			m_bChanged= false;
		}
	}
	return true;
}

EStateFTDI CPinWPeriph::GetState()
{
	EnterCriticalSection(&m_hDataSafe);
	EStateFTDI eState= m_eState;
	LeaveCriticalSection(&m_hDataSafe);
	return eState;
}

void CPinWPeriph::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if(m_bError)
		return;
	if (!pHead || dwSize < sizeof(SBaseIn) + sizeof(SBase) || ((SBaseIn*)pHead)->dwSize != dwSize || ((SBaseIn*)pHead)->eType != eData ||
		(!(((SBase*)((char*)pHead+sizeof(SBaseIn)))->eType == eFTDIPinWDataArray && dwSize > sizeof(SBaseIn) + sizeof(SBase) && 
		!((dwSize-sizeof(SBase)-sizeof(SBaseIn))%sizeof(SPinWData))) && !(((SBase*)((char*)pHead+sizeof(SBaseIn)))->eType == eFTDIPinWDataBufArray && 
		dwSize > sizeof(SBaseIn)+sizeof(SBase)+sizeof(SPinWData))))	// there has to be at least some data
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

	int nLen= 0;
	unsigned char* aucData= NULL;
	SPinWData* asPinData= NULL;
	SPinWData sPinData;
	unsigned char ucPinSelect;
	int nError= 0;
	if (((SBaseIn*)((char*)pHead+sizeof(SBaseIn)))->eType == eFTDIPinWDataArray)
	{
		nLen= (dwSize-sizeof(SBaseIn)-sizeof(SBase))/sizeof(SPinWData);
		asPinData= (SPinWData*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn));
		int nSum= 0;
		for (int i= 0; i<nLen; ++i)
			nSum += asPinData[i].usRepeat;
		if (nSum>m_sInit.usBytesUsed)
			nError= BAD_INPUT_PARAMS;
	} else
	{
		nLen= dwSize-sizeof(SBaseIn)-sizeof(SBase)-sizeof(SPinWData);
		aucData= (unsigned char*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn)+sizeof(SPinWData));
		sPinData= *(SPinWData*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn));
		sPinData.ucPinSelect= sPinData.ucPinSelect&(~m_ucMask);
		ucPinSelect= ~sPinData.ucPinSelect;
		if (nLen>m_sInit.usBytesUsed)
			nError= BAD_INPUT_PARAMS;
	}
	if (!nError)
	{
		EnterCriticalSection(&m_hDataSafe);
		if (m_eState == eActive)
		{
			if (((SBaseIn*)((char*)pHead+sizeof(SBaseIn)))->eType == eFTDIPinWDataArray)
			{
				int i= 0;
				int k= 0;
				unsigned char ucVal;
				for (;i<nLen; ++i)
				{
					asPinData[i].ucPinSelect= asPinData[i].ucPinSelect&(~m_ucMask);	// make sure we only touch valid pins
					ucPinSelect= ~asPinData[i].ucPinSelect;					// pins we don't want to update stay the same
					ucVal= asPinData[i].ucValue&asPinData[i].ucPinSelect;	// select pins we want to update
					int kTemp= k;
					for (; k < kTemp+asPinData[i].usRepeat; ++k)
						m_aucData[k]= (m_aucData[k]&ucPinSelect)|ucVal;
				}
				for (;k<m_sInit.usBytesUsed; ++k)
					m_aucData[k]= (m_aucData[k]&ucPinSelect)|ucVal;
			} else
			{
				int i= 0;
				for (;i<nLen; ++i)
					m_aucData[i]= (m_aucData[i]&ucPinSelect)|(aucData[i]&sPinData.ucPinSelect);
				const unsigned char ucVal= aucData[i-1]&sPinData.ucPinSelect;	// padd the same data until end of array
				for (;i<m_sInit.usBytesUsed; ++i)
					m_aucData[i]= (m_aucData[i]&ucPinSelect)|ucVal;
			}
			m_bUpdated= true;
			m_allIds.Push(llId);	// pipe to notify of success
			SetEvent(m_hNext);		// update only in cc
		} else
			nError= INACTIVE_DEVICE;
		LeaveCriticalSection(&m_hDataSafe);
	}

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



CPinRPeriph::CPinRPeriph(const SPinInit &sPinInit, CComm *pcComm, const SInitPeriphFT &sInitFT, 
	int &nError, HANDLE hNewData, CTimer* pcTimer) : CPeriphFTDI(PIN_R_P, sInitFT), m_sInit(sPinInit),
	m_ucMask(~sPinInit.ucActivePins), m_allIds(hNewData)
{
	nError= 0;
	m_bError= true;
	InitializeCriticalSection(&m_hDataSafe);
	m_pcMemPool= new CMemPool;
	// minw has to be at least as large as required for writing full boards, dwbuff also has to be as large
	if (!pcComm || sInitFT.dwMinSizeW < sPinInit.usBytesUsed ||
		sInitFT.dwBuff < sInitFT.dwMinSizeW || sInitFT.dwMinSizeW != sInitFT.dwMinSizeR || 
		!sPinInit.usBytesUsed || !sPinInit.ucActivePins || !pcTimer || !hNewData)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	m_pcComm= pcComm;
	m_pcTimer= pcTimer;
	m_bRead= false;
	m_nProcessed= 0;
	m_bError= false;
	m_eState= eInactivateState;
	m_hNext= hNewData;	// you set this when new data is availible
}

DWORD CPinRPeriph::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT);
	if (dwSize<sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT);
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

	((SBase*)pHead)->dwSize= sizeof(SPinInit)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIPinReadInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sInit, sizeof(SPinInit));

	return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(SPinInit)+sizeof(SInitPeriphFT);
}

CPinRPeriph::~CPinRPeriph()
{
	DeleteCriticalSection(&m_hDataSafe);
	delete m_pcMemPool;
}

bool CPinRPeriph::DoWork(void *pHead, DWORD dwSize, FT_HANDLE ftHandle, EStateFTDI eReason, int nError)
{
	if (m_bError)
		return true;
	unsigned char *aucBuff= (unsigned char *)pHead;
	bool bNotEmpty;

	if (eReason == eActivateState || eReason == eInactivateState)	// update state
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eReason == eActivateState?eActive:eInactivateState;
		LeaveCriticalSection(&m_hDataSafe);
	}

	if (m_eState == eInactivateState && eReason == ePreWrite)	// shut it down
	{
		EnterCriticalSection(&m_hDataSafe);
		m_eState= eInactive;
		LeaveCriticalSection(&m_hDataSafe);
		ResetEvent(m_hNext);
		for (int i= 0; i<m_allIds.GetSize(); ++i)	// respond to all waiting users
		{
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
				m_pcComm->SendData(&sData, m_allIds.Front(true, bNotEmpty));
			}
		}
		m_nProcessed= 0;
		m_bRead= false;
		for (DWORD i= 0; i<m_sInitFT.dwBuff;++i)				// now set these pins to default
			aucBuff[i]= aucBuff[i]&m_ucMask;
	} else if (m_eState == eActive && eReason == ePreWrite)	// write the next set of data
	{
		m_nProcessed= m_allIds.GetSize();	// current number of triggers proccesed (only one if cont)
		if (m_nProcessed)
		{
			m_bRead= true;
			m_dInitial= m_pcTimer->Seconds();	// time right before reading
		}
	} else if (m_eState == eActive && eReason == ePostWrite) // let user know if error and set buffer to default.
	{
		if (m_bRead)
		{
			if (nError)
			{
				for (int i= 0; i<m_nProcessed; ++i)	// if error, notfiy here
				{
					SData sData;
					sData.dwSize= sizeof(SBaseOut);
					sData.pDevice= this;
					sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseOut));
					if (sData.pHead)
					{
						((SBaseOut*)sData.pHead)->sBaseIn.eType= eResponseExD;
						((SBaseOut*)sData.pHead)->sBaseIn.nChan= m_sInitFT.nChan;
						((SBaseOut*)sData.pHead)->sBaseIn.nError= nError;
						((SBaseOut*)sData.pHead)->sBaseIn.dwSize= sizeof(SBaseOut);
						((SBaseOut*)sData.pHead)->dDouble= m_dInitial;
						m_pcComm->SendData(&sData, m_allIds.Front(!m_sInit.bContinuous, bNotEmpty));	// if cont, than only one user, don't pop
					}
				}
				m_bRead= false;
				m_nProcessed= 0;
				EnterCriticalSection(&m_hDataSafe);
				if (!m_allIds.GetSize())
					ResetEvent(m_hNext);
				LeaveCriticalSection(&m_hDataSafe);
			}
		}
	} else if (m_eState == eActive && eReason == ePostRead) // let user know if error and set buffer to default.
	{
		if (m_bRead)
		{
			for (int i= 0; i<m_nProcessed; ++i)	// notify users
			{
				SData sData;
				sData.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(unsigned char)*m_sInit.usBytesUsed;
				sData.pDevice= this;
				sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
				if (sData.pHead)
				{
					((SBaseOut*)sData.pHead)->sBaseIn.eType= eResponseExD;
					((SBaseOut*)sData.pHead)->sBaseIn.nChan= m_sInitFT.nChan;
					((SBaseOut*)sData.pHead)->sBaseIn.nError= nError;
					((SBaseOut*)sData.pHead)->sBaseIn.dwSize= sData.dwSize;
					((SBaseOut*)sData.pHead)->dDouble= m_dInitial;
					((SBase*)((unsigned char*)sData.pHead+sizeof(SBaseOut)))->eType= eFTDIPinRDataArray;
					((SBase*)((unsigned char*)sData.pHead+sizeof(SBaseOut)))->dwSize= sizeof(SBase)+sizeof(unsigned char)*m_sInit.usBytesUsed;
					unsigned char* ucVal= (unsigned char*)sData.pHead+sizeof(SBaseOut)+sizeof(SBase);
					for (unsigned short i= 0; i<m_sInit.usBytesUsed; ++i)
						ucVal[i]= ((SFTBufferSafe*)pHead)->aucBuff[i]&~m_ucMask;
					m_pcComm->SendData(&sData, m_allIds.Front(!m_sInit.bContinuous, bNotEmpty));	// if cont, than only one user, don't pop
				}
			}
			m_bRead= false;
			m_nProcessed= 0;
			EnterCriticalSection(&m_hDataSafe);
			if (!m_allIds.GetSize())
				ResetEvent(m_hNext);
			LeaveCriticalSection(&m_hDataSafe);
		}
	}
	return true;
}

EStateFTDI CPinRPeriph::GetState()
{
	EnterCriticalSection(&m_hDataSafe);
	EStateFTDI eState= m_eState;
	LeaveCriticalSection(&m_hDataSafe);
	return eState;
}

void CPinRPeriph::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if(m_bError)
		return;
	// only trigger and only accept a trigger for cont if it's the first
	if (!pHead || dwSize != sizeof(SBaseIn) || ((SBaseIn*)pHead)->eType != eTrigger ||
		((SBaseIn*)pHead)->dwSize != dwSize || (m_sInit.bContinuous && m_allIds.GetSize())) 
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
	EnterCriticalSection(&m_hDataSafe);
	if (m_eState == eActive)
	{
		m_allIds.Push(llId);	// pipe to send reads
		SetEvent(m_hNext);
	} else
		nError= INACTIVE_DEVICE;
	LeaveCriticalSection(&m_hDataSafe);

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