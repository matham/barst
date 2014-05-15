
#include "base classses.h"
#include "named pipes.h"
#include "ftdi device.h"
#include "ftd2xx.h"
#include "misc tools.h"
#include <iostream>

FT_STATUS (WINAPI *lpfFT_GetDeviceInfoList)(FT_DEVICE_LIST_INFO_NODE *pDest, LPDWORD lpdwNumDevs)= NULL;
FT_STATUS (WINAPI *lpfFT_CreateDeviceInfoList)(LPDWORD lpdwNumDevs)= NULL;
FT_STATUS (WINAPI *lpfFT_SetLatencyTimer)(FT_HANDLE ftHandle, UCHAR ucLatency)= NULL;
FT_STATUS (WINAPI *lpfFT_OpenEx)(PVOID pArg1, DWORD Flags, FT_HANDLE *pHandle)= NULL;
FT_STATUS (WINAPI *lpfFT_Close)(FT_HANDLE ftHandle)= NULL;
FT_STATUS (WINAPI *lpfFT_Read)(FT_HANDLE ftHandle, LPVOID lpBuffer, DWORD dwBytesToRead, LPDWORD lpBytesReturned)= NULL;
FT_STATUS (WINAPI *lpfFT_Write)(FT_HANDLE ftHandle, LPVOID lpBuffer, DWORD dwBytesToWrite, LPDWORD lpBytesWritten)= NULL;
FT_STATUS (WINAPI *lpfFT_SetBaudRate)(FT_HANDLE ftHandle, ULONG BaudRate)= NULL;
FT_STATUS (WINAPI *lpfFT_SetBitMode)(FT_HANDLE ftHandle, UCHAR ucMask, UCHAR ucEnable)= NULL;
FT_STATUS (WINAPI *lpfFT_Purge)(FT_HANDLE ftHandle, ULONG Mask)= NULL;
FT_STATUS (WINAPI *lpfFT_SetUSBParameters)(FT_HANDLE ftHandle, ULONG ulInTransferSize, ULONG ulOutTransferSize)= NULL;
FT_STATUS (WINAPI *lpfFT_CyclePort)(FT_HANDLE ftHandle)= NULL;
FT_STATUS (WINAPI *lpfFT_ResetPort)(FT_HANDLE ftHandle)= NULL;
FT_STATUS (WINAPI *lpf_FT_GetLibraryVersion)(LPDWORD lpdwVersion)= NULL;



CManagerFTDI::CManagerFTDI(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError) : 
	CManager(FTDI_MAN_STR, std::tstring(szPipe), nChan)
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
		_T("ftd2xx64.dll")
#else
		_T("ftd2xx.dll")
#endif
		, 14, &lpfFT_GetDeviceInfoList, "FT_GetDeviceInfoList", 
		&lpfFT_CreateDeviceInfoList, "FT_CreateDeviceInfoList", 
		&lpfFT_SetLatencyTimer, "FT_SetLatencyTimer", &lpfFT_OpenEx, "FT_OpenEx", &lpfFT_Close, "FT_Close", 
		&lpfFT_Read, "FT_Read", &lpfFT_Write, "FT_Write", &lpfFT_SetBaudRate, "FT_SetBaudRate", &lpfFT_SetBitMode, "FT_SetBitMode",
		&lpfFT_Purge, "FT_Purge", &lpfFT_SetUSBParameters, "FT_SetUSBParameters", &lpfFT_CyclePort, "FT_CyclePort",
		&lpfFT_ResetPort, "FT_ResetPort", &lpf_FT_GetLibraryVersion, "FT_GetLibraryVersion");
	if (!bRes || !m_hLib)
	{
		nError= DRIVER_ERROR;
		return;
	}
	DWORD dwVersion;
	if ((nError= FT_ERROR(lpf_FT_GetLibraryVersion(&dwVersion), nError)) != 0)
		return;
	if (dwVersion < MIN_FTDI_LIB_VER)
	{
		nError= DRIVER_ERROR;
		return;
	}
	m_dwNumDevs= 0;
	m_bError= false;
	m_pcComm= pcComm;
}

DWORD CManagerFTDI::GetInfo(void* pHead, DWORD dwSize)
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

CManagerFTDI::~CManagerFTDI()
{
	for (size_t i= 0; i<m_acFTDevices.size(); ++i)
		delete m_acFTDevices[i];
	if (m_hLib != NULL)
        FreeLibrary(m_hLib);
	delete m_pcMemPool;
}

void CManagerFTDI::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
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
		bRes= false;
		sBase.eType= eQuery;
		if (((SBaseIn*)pHead)->nChan == -1)	// send all devices if chan is -1
		{
			
			sBase.nError= FT_ERROR(lpfFT_CreateDeviceInfoList(&m_dwNumDevs), sBase.nError);	// reload deice list
			if (!sBase.nError)
			{
				m_asFTChanInfo.assign(max(m_dwNumDevs, 1), FT_DEVICE_LIST_INFO_NODE());
				sBase.nError= FT_ERROR(lpfFT_GetDeviceInfoList(&m_asFTChanInfo[0], &m_dwNumDevs), sBase.nError);	// copy back device list from driver
				if (!sBase.nError)
				{
					DWORD i= 0, dwInfoSize= 0;
					std::vector<int> anDev;
					anDev.assign(max(m_dwNumDevs, 1), -1);
					// find the total size of the info
					for (; i<m_dwNumDevs; ++i)	// each device compare to opened device to see if requesting open device
					{
						m_asFTChanInfo[i].ftHandle= NULL;	// so that we can compare
						m_asFTChanInfo[i].Flags &= ~FT_FLAGS_OPENED;	// reset this flag because initially unopened
						memset(&m_asFTChanInfo[i].Description[strlen(m_asFTChanInfo[i].Description)], 0, 
							sizeof(m_asFTChanInfo[i].Description)-strlen(m_asFTChanInfo[i].Description));
						memset(&m_asFTChanInfo[i].SerialNumber[strlen(m_asFTChanInfo[i].SerialNumber)], 0, 
							sizeof(m_asFTChanInfo[i].SerialNumber)-strlen(m_asFTChanInfo[i].SerialNumber));
						size_t j= 0;
						for (;j<m_acFTDevices.size(); ++j)	// all open devices
						{
							if (m_acFTDevices[j])	// may be null
							{
								if (!memcmp(&m_asFTChanInfo[i], &m_acFTDevices[j]->m_FTInfo, sizeof(FT_DEVICE_LIST_INFO_NODE)))
								{
									anDev[i]= j;	// this device is open
									break;
								}
							}
						}
						if (anDev[i] == -1)	// didn't find; un-opened channel
						{
							dwInfoSize+= sizeof(SBaseOut)+sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS);
						} else				// get info from that channel
						{
							dwInfoSize+= m_acFTDevices[j]->GetInfo(NULL, 0);
						}
					}
					i= 0;
					// now copy the info
					if (dwInfoSize)
					{
						dwInfoSize+= sizeof(SBaseIn);
						pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(dwInfoSize);
						SBaseOut* pBaseO= (SBaseOut*)((char*)pBase+sizeof(SBaseIn));	// current position
						if(pBase)
						{
							memset(pBase, 0, dwInfoSize);
							for (; i<m_dwNumDevs; ++i)	
							{
								if (anDev[i] == -1)	// didn't find, un-opened channel
								{
									pBaseO->sBaseIn.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS);
									pBaseO->sBaseIn.eType= eResponseEx;
									pBaseO->sBaseIn.nChan= -1;
									SBase* pBaseS= (SBase*)(++pBaseO);
									pBaseS->dwSize= sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS);
									pBaseS->eType= eFTDIChan;
									memcpy(++pBaseS, &m_asFTChanInfo[i], sizeof(FT_DEVICE_LIST_INFO_NODE));
									pBaseO= (SBaseOut*)((char*)pBaseS+sizeof(FT_DEVICE_LIST_INFO_NODE_OS));
								} else				// get info from that channel
								{
									m_acFTDevices[anDev[i]]->GetInfo(pBaseO, dwInfoSize-(int)((char*)pBaseO-(char*)pBase));
									pBaseO= (SBaseOut*)((char*)pBaseO+m_acFTDevices[anDev[i]]->GetInfo(NULL, 0));
								}
							}
							pBase->dwSize= dwInfoSize;
							pBase->eType= eQuery;
							pBase->nChan= -1;
							pBase->nError= 0;
							sData.dwSize= dwInfoSize;
							sData.pHead= pBase;
							m_pcComm->SendData(&sData, llId);
						}
					} else
						sBase.nError= NO_CHAN;
				}
			}
		} else if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acFTDevices.size() || 
			!m_acFTDevices[((SBaseIn*)pHead)->nChan])	// invalid channel
		{
			sBase.nError= INVALID_CHANN;
		} else			// send info on particular chann
		{
			DWORD dwSizeInfo= m_acFTDevices[((SBaseIn*)pHead)->nChan]->GetInfo(NULL, 0);
			pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(dwSizeInfo);
			if (pBase)
			{
				m_acFTDevices[((SBaseIn*)pHead)->nChan]->GetInfo(pBase, dwSizeInfo);
				sData.dwSize= dwSizeInfo;
				sData.pHead= pBase;
				m_pcComm->SendData(&sData, llId);
			}
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eDelete)	// delete a channel
	{
		sBase.eType= eDelete;
		if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acFTDevices.size() || 
			!m_acFTDevices[((SBaseIn*)pHead)->nChan])
			sBase.nError= INVALID_CHANN;
		else
		{
			delete m_acFTDevices[((SBaseIn*)pHead)->nChan];
			m_acFTDevices[((SBaseIn*)pHead)->nChan]= NULL;
			sBase.nChan= ((SBaseIn*)pHead)->nChan;
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eVersion && 
		((SBaseIn*)pHead)->nChan == -1)	// delete a channel
	{
		sBase.nError= FT_ERROR(lpf_FT_GetLibraryVersion(&sBase.dwInfo), sBase.nError);
		sBase.eType= eVersion;
	} else if (((SBaseIn*)pHead)->nChan >= 0 && ((SBaseIn*)pHead)->nChan < (int)m_dwNumDevs && 
		((SBaseIn*)pHead)->eType == eSet && ((SBaseIn*)pHead)->dwSize > sizeof(SBaseIn)+sizeof(SBase)+
		sizeof(SChanInitFTDI) && ((SBase*)((char*)pHead+sizeof(SBaseIn)))->eType == eFTDIChanInit)	// set a channel
	{
		bRes= false;
		sBase.eType= eSet;
		size_t i= 0;
		LARGE_INTEGER llStart;
		for (; i<m_acFTDevices.size() && m_acFTDevices[i]; ++i);	// get index where we add new channel
		if (i == m_acFTDevices.size())
			m_acFTDevices.push_back(NULL);

		std::tstringstream ss;	// ftdi channel
		ss<<i;
		std::tstringstream ss2;	// ftdi manager index
		ss2<<m_nChan;
		std::tstring csPipeName= m_csPipeName+_T(":")+ss2.str()+_T(":")+ss.str(); // new channel pipe name
		//SChanInitFTDI sChan= *(SChanInitFTDI*)((char*)pHead+sizeof(SBaseIn));
		CChannelFTDI* pcChan= new CChannelFTDI(*(SChanInitFTDI*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn)), 
			(SBase*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn)+sizeof(SChanInitFTDI)), 
			dwSize-sizeof(SBase)-sizeof(SBaseIn)-sizeof(SChanInitFTDI),
			m_asFTChanInfo[((SBaseIn*)pHead)->nChan], csPipeName.c_str(), i, sBase.nError, llStart);
		if (!sBase.nError)
		{
			m_acFTDevices[i]= pcChan;
			SBaseOut* pBaseO= (SBaseOut*)m_pcMemPool->PoolAcquire(sizeof(SBaseOut));
			if (pBaseO)
			{
				pBaseO->sBaseIn.dwSize= sizeof(SBaseOut);
				pBaseO->sBaseIn.eType= eResponseExL;
				pBaseO->sBaseIn.nChan= i;
				pBaseO->sBaseIn.nError= 0;
				pBaseO->llLargeInteger= llStart;
				pBaseO->bActive= true;
				sData.pHead= pBaseO;
				sData.dwSize= sizeof(SBaseOut);
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





DWORD WINAPI FTChanProc(LPVOID lpParameter)
{
	// Call ThreadProc function of ftdi channel
    return ((CChannelFTDI*)lpParameter)->ThreadProc();
}

CChannelFTDI::CChannelFTDI(SChanInitFTDI &sChanInit, SBase* pInit, DWORD dwSize, FT_DEVICE_LIST_INFO_NODE &sFTInfo, 
	const TCHAR szPipe[], int nChan, int &nError, LARGE_INTEGER &llStart) : CDevice(FTDI_CHAN_STR), m_FTInfo(sFTInfo), 
	m_csPipeName(szPipe), m_hUpdate(CreateEvent(NULL,TRUE, TRUE, NULL)), m_asUpdates(m_hUpdate), m_sChanInit(sChanInit), 
	m_nChan(nChan)
{
	m_bError= true;
	m_ftHandle= NULL;
	m_hStop= NULL;
	m_hNext= NULL;
	m_hThread= NULL;
	m_aucTx= NULL;
	m_sRx1.aucBuff= NULL;
	m_sRx2.aucBuff= NULL;
	m_pcComm= NULL;
	m_pcMemPool= new CMemPool;

	nError= 0;
	InitializeCriticalSection(&m_sRx1.sSafe);
	InitializeCriticalSection(&m_sRx2.sSafe);

	if (!pInit || !szPipe)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}

	// open device by loc
	nError= FT_ERROR(lpfFT_OpenEx((PVOID)m_FTInfo.LocId, FT_OPEN_BY_LOCATION, &m_ftHandle), nError);
	if (nError)
		return;
	// shouldn't ever need to time out
	nError= FT_ERROR(lpfFT_SetLatencyTimer(m_ftHandle, 255), nError);
	if (nError)
		return;

	m_hStop= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hNext= CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!m_hStop || !m_hNext || !m_hUpdate)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}
	m_ahEvents.push_back(m_hStop);
	m_ahEvents.push_back(m_hUpdate);
	m_ahEvents.push_back(m_hNext);

	
	SInitPeriphFT sPeriphInit;
	SBase *pBase= pInit;
	DWORD dwMaxWrite= 0;	// XXX: if we ever read more than write we will have to verify that it's large enough (since we equate them later).
	bool bPadd= false;	// if we pad r/w packets to match 62/510 boundries
	// writing multiples of 510/62 makes it faster
	const DWORD dwPacket= sFTInfo.Flags&FT_FLAGS_HISPEED ? 510 : 62;
	// the max size we can ever write
	const DWORD dwMaxPacket= sFTInfo.Flags&FT_FLAGS_HISPEED ? FTDI_MAX_BUFF_H : FTDI_MAX_BUFF_L;
	DWORD dwBuffSizeRead= sChanInit.dwBuffIn;
	DWORD dwBuffSizeWrite= sChanInit.dwBuffOut;
	// we first need to find the max write buffer so we can pass it to all devices
	// at each step we make sure dwMaxWrite doesn't become larger than dwMaxPacket
	while ((char*)pBase + sizeof(SBase) <= (char*)pInit + dwSize)
	{
		switch (pBase->eType)
		{
		case eFTDIMultiWriteInit:
			if ((char*)pBase + pBase->dwSize <= (char*)pInit + dwSize && pBase->dwSize == sizeof(SValveInit)+sizeof(SBase))
			{
				SValveInit* psValveInit= (SValveInit*)++pBase;
				dwMaxWrite= max((psValveInit->dwBoards*8*2+2)*psValveInit->dwClkPerData, dwMaxWrite);
				dwBuffSizeRead= max(dwBuffSizeRead, psValveInit->dwBoards*8*sizeof(bool));
				pBase= (SBase*)((char*)pBase +sizeof(SValveInit));
				if ((psValveInit->dwBoards*8*2+2)*psValveInit->dwClkPerData > dwMaxPacket)
				{
					nError= BUFF_TOO_SMALL;
					return;
				}
			} else
			{
				nError= SIZE_MISSMATCH;
				return;
			}
			break;
		case eFTDIMultiReadInit:
			if ((char*)pBase + pBase->dwSize <= (char*)pInit + dwSize && pBase->dwSize == sizeof(SValveInit)+sizeof(SBase))
			{
				SValveInit* psValveInit= (SValveInit*)++pBase;
				dwMaxWrite= max((psValveInit->dwBoards*8*2+2+4)*psValveInit->dwClkPerData, dwMaxWrite);
				dwBuffSizeWrite= max(dwBuffSizeWrite, psValveInit->dwBoards*8*sizeof(bool));
				pBase= (SBase*)((char*)pBase +sizeof(SValveInit));
				bPadd= true;
				if ((psValveInit->dwBoards*8*2+2+4)*psValveInit->dwClkPerData > dwMaxPacket)
				{
					nError= BUFF_TOO_SMALL;
					return;
				}
			} else
			{
				nError= SIZE_MISSMATCH;
				return;
			}
			break;
		case eFTDIPinWriteInit:
			if ((char*)pBase + pBase->dwSize <= (char*)pInit + dwSize && pBase->dwSize == sizeof(SPinInit)+sizeof(SBase))
			{
				SPinInit* psPinInit= (SPinInit*)++pBase;
				dwMaxWrite= max(psPinInit->usBytesUsed, dwMaxWrite);
				dwBuffSizeRead= max(dwBuffSizeRead, psPinInit->usBytesUsed*sizeof(SPinWData));
				pBase= (SBase*)((char*)pBase +sizeof(SPinInit));
				if (psPinInit->usBytesUsed > dwMaxPacket)
				{
					nError= BUFF_TOO_SMALL;
					return;
				}
			} else
			{
				nError= SIZE_MISSMATCH;
				return;
			}
			break;
		case eFTDIPinReadInit:
			if ((char*)pBase + pBase->dwSize <= (char*)pInit + dwSize && pBase->dwSize == sizeof(SPinInit)+sizeof(SBase))
			{
				SPinInit* psPinInit= (SPinInit*)++pBase;
				dwMaxWrite= max(psPinInit->usBytesUsed, dwMaxWrite);
				dwBuffSizeWrite= max(dwBuffSizeWrite, psPinInit->usBytesUsed);
				pBase= (SBase*)((char*)pBase +sizeof(SPinInit));
				bPadd= true;
				if (psPinInit->usBytesUsed > dwMaxPacket)
				{
					nError= BUFF_TOO_SMALL;
					return;
				}
			} else
			{
				nError= SIZE_MISSMATCH;
				return;
			}
			break;
		case eFTDIADCInit:
			if ((char*)pBase + pBase->dwSize <= (char*)pInit + dwSize && pBase->dwSize == sizeof(SADCInit)+sizeof(SBase))
			{
				++pBase;
				SADCInit* sADCInit= (SADCInit*)pBase;
				if (sADCInit->fUSBBuffToUse >= 100.0)
					dwMaxWrite= max(dwMaxPacket, dwMaxWrite);
				else if (sADCInit->fUSBBuffToUse/100 <= (float)dwPacket/(float)dwMaxPacket)
					dwMaxWrite= max(dwPacket, dwMaxWrite);
				else 
					dwMaxWrite= max((DWORD)ceil(sADCInit->fUSBBuffToUse/100*dwMaxPacket)-(DWORD)ceil(sADCInit->fUSBBuffToUse/100*dwMaxPacket)%dwPacket, dwMaxWrite);
				bPadd= true;
				pBase= (SBase*)((char*)pBase +sizeof(SADCInit));
				dwBuffSizeWrite= max(dwBuffSizeWrite, sizeof(SADCInit)+sizeof(DWORD)*sADCInit->dwDataPerTrans*(sADCInit->bChan2?2:1));
			} else
			{
				nError= SIZE_MISSMATCH;
				return;
			}
			break;
		default:
			nError= INVALID_DEVICE;
			return;
		}
	}
	if (bPadd)	// align to 510/62
		dwMaxWrite= dwPacket*(DWORD)ceil((double)dwMaxWrite/dwPacket);

	m_pcComm= new CPipeServer;	// our pipe over which comm to devices will occur
	nError= static_cast<CPipeServer*>(m_pcComm)->Init(szPipe, ~0x80000000, dwBuffSizeRead+MIN_BUFF_IN, 
		dwBuffSizeWrite+MIN_BUFF_OUT, this, NULL);
	if (nError)
		return;

	// now we decode which devices to create and verify them
	// ADC buffer size MUST be as large as dwBuff, i.e. it must be the largest user
	pBase= (SBase*)pInit;
	HANDLE hEvent;
	while ((char*)pBase + sizeof(SBase) <= (char*)pInit + dwSize)
	{
		switch (pBase->eType)
		{
		case eFTDIMultiWriteInit:
			{
			SValveInit sValveInit= *(SValveInit*)(++pBase);
			sPeriphInit.dwMinSizeR= 0;
			sPeriphInit.dwMinSizeW= (sValveInit.dwBoards*8*2+2)*sValveInit.dwClkPerData;
			sPeriphInit.nChan= (int)m_aDevices.size();
			sPeriphInit.ucBitMode= FT_BITMODE_ASYNC_BITBANG|FT_BITMODE_SYNC_BITBANG;
			switch(sFTInfo.Type)
			{
			case FT_DEVICE_2232H:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_2232H;	// 1MHz/5
				break;
			default:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_DEFAULT;	// 1MHz/16
				break;
			}
			sPeriphInit.ucBitOutput= 1<<sValveInit.ucLatch|1<<sValveInit.ucClk|1<<sValveInit.ucData;
			sPeriphInit.dwBuff= dwMaxWrite;
			hEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
			m_ahEvents.push_back(hEvent);
			m_aDevices.push_back(new CMultiWPeriph(sValveInit, m_pcComm, sPeriphInit, nError, hEvent, &m_cTimer));
			if (nError)
				return;
			pBase= (SBase*)((char*)pBase +sizeof(SValveInit));
			break;
			}
		case eFTDIMultiReadInit:
			{
			SValveInit sValveInit= *(SValveInit*)(++pBase);
			sPeriphInit.dwMinSizeR= dwPacket*(DWORD)ceil((sValveInit.dwBoards*8*2+2+4)*sValveInit.dwClkPerData/(double)dwPacket);
			sPeriphInit.dwMinSizeW= sPeriphInit.dwMinSizeR;
			sPeriphInit.nChan= (int)m_aDevices.size();
			sPeriphInit.ucBitMode= FT_BITMODE_SYNC_BITBANG;
			switch(sFTInfo.Type)
			{
			case FT_DEVICE_2232H:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_2232H;	// 1MHz/5
				break;
			default:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_DEFAULT;	// 1MHz/16
				break;
			}
			sPeriphInit.ucBitOutput= 1<<sValveInit.ucLatch|1<<sValveInit.ucClk;
			sPeriphInit.dwBuff= dwMaxWrite;
			hEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
			m_ahEvents.push_back(hEvent);
			m_aDevices.push_back(new CMultiRPeriph(sValveInit, m_pcComm, sPeriphInit, nError, hEvent, &m_cTimer));
			if (nError)
				return;
			pBase= (SBase*)((char*)pBase +sizeof(SValveInit));
			break;
			}
		case eFTDIPinWriteInit:
			{
			SPinInit sPinInit= *(SPinInit*)(++pBase);
			sPeriphInit.dwMinSizeR= 0;
			sPeriphInit.dwMinSizeW= sPinInit.usBytesUsed;
			sPeriphInit.nChan= (int)m_aDevices.size();
			sPeriphInit.ucBitMode= FT_BITMODE_ASYNC_BITBANG|FT_BITMODE_SYNC_BITBANG;
			switch(sFTInfo.Type)
			{
			case FT_DEVICE_2232H:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_2232H;	// 1MHz/5
				break;
			default:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_DEFAULT;	// 1MHz/16
				break;
			}
			sPeriphInit.ucBitOutput= sPinInit.ucActivePins;
			sPeriphInit.dwBuff= dwMaxWrite;
			hEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
			m_ahEvents.push_back(hEvent);
			m_aDevices.push_back(new CPinWPeriph(sPinInit, m_pcComm, sPeriphInit, nError, hEvent, &m_cTimer));
			if (nError)
				return;
			pBase= (SBase*)((char*)pBase +sizeof(SPinInit));
			break;
			}
		case eFTDIPinReadInit:
			{
			SPinInit sPinInit= *(SPinInit*)(++pBase);
			sPeriphInit.dwMinSizeR= dwPacket*(DWORD)ceil(sPinInit.usBytesUsed/(double)dwPacket);
			sPeriphInit.dwMinSizeW= sPeriphInit.dwMinSizeR;
			sPeriphInit.nChan= (int)m_aDevices.size();
			sPeriphInit.ucBitMode= FT_BITMODE_SYNC_BITBANG;
			switch(sFTInfo.Type)
			{
			case FT_DEVICE_2232H:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_2232H;	// 1MHz/5
				break;
			default:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_DEFAULT;	// 1MHz/16
				break;
			}
			sPeriphInit.ucBitOutput= 0;
			sPeriphInit.dwBuff= dwMaxWrite;
			hEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
			m_ahEvents.push_back(hEvent);
			m_aDevices.push_back(new CPinRPeriph(sPinInit, m_pcComm, sPeriphInit, nError, hEvent, &m_cTimer));
			if (nError)
				return;
			pBase= (SBase*)((char*)pBase +sizeof(SPinInit));
			break;
			}
		case eFTDIADCInit:
			{
			SADCInit sADCInit= *(SADCInit*)(++pBase);
			sPeriphInit.dwBuff= dwMaxWrite;
			sPeriphInit.dwMinSizeR= dwMaxWrite;
			sPeriphInit.dwMinSizeW= dwMaxWrite;
			sPeriphInit.nChan= (int)m_aDevices.size();
			sPeriphInit.ucBitMode= FT_BITMODE_SYNC_BITBANG;
			switch(sFTInfo.Type)
			{
			case FT_DEVICE_2232H:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_2232H;	// 1MHz/5
				break;
			default:
				sPeriphInit.dwMaxBaud= FTDI_BAUD_DEFAULT;	// 1MHz/16
				break;
			}
			sPeriphInit.ucBitOutput= 1<<sADCInit.ucClk;
			hEvent= CreateEvent(NULL,TRUE, FALSE, NULL);
			m_ahEvents.push_back(hEvent);
			m_aDevices.push_back(new CADCPeriph(sADCInit, m_pcComm, sPeriphInit, hEvent, nError, &m_cTimer));
			if (nError)
				return;
			pBase= (SBase*)((char*)pBase +sizeof(SADCInit));
			break;
			}
		default:
			nError= INVALID_DEVICE;
			return;
		}
	}
	if (!m_aDevices.size())	// no devcies added
	{
		nError= NO_CHAN;
		return;
	}
	// we cannot wait on more events
	if (m_ahEvents.size() > MAXIMUM_WAIT_OBJECTS)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}

	// verify values
	unsigned char ucMode= 0xFF;
	DWORD dwWrite= 0, dwRead= 0;
	for (DWORD i= 0; i<m_aDevices.size(); ++i)
	{
		ucMode &= m_aDevices[i]->m_sInitFT.ucBitMode;
		dwWrite= max(m_aDevices[i]->m_sInitFT.dwMinSizeW, dwWrite);
		dwRead= max(m_aDevices[i]->m_sInitFT.dwMinSizeR, dwRead);
	}
	ucMode= ucMode&(~ucMode+1);	// lowest bit (async bit bang mode preffered)
	if (ucMode&FT_BITMODE_SYNC_BITBANG)	// if Read is less than write (for diff devices), in combo read might be less than write in bit bang mode
	{
		dwWrite= max(dwWrite, dwRead);
		dwRead= dwWrite;
	}
	// at least one output mode (sync/async bit bang..., )
	if (!ucMode || dwRead>dwMaxPacket || dwWrite>dwMaxPacket || !dwWrite ||
		(dwRead && !dwWrite))
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	// we never read without writing
	if (dwWrite)
	{
		m_aucTx= (unsigned char*)m_pcMemPool->PoolAcquire(dwWrite);
		if (!m_aucTx)
		{
			nError= NO_SYS_RESOURCE;
			return;
		}
	}
	if (dwRead)
	{
		m_sRx1.aucBuff= (unsigned char*)m_pcMemPool->PoolAcquire(dwRead+2);
		m_sRx2.aucBuff= (unsigned char*)m_pcMemPool->PoolAcquire(dwRead+2);
		if (!m_sRx1.aucBuff || !m_sRx2.aucBuff)
		{
			nError= NO_SYS_RESOURCE;
			return;
		}
		m_sRx1.aucBuff+= 2;
		m_sRx2.aucBuff+= 2;
	}

	m_hThread= CreateThread(NULL, 0, FTChanProc, this, 0, NULL);
	if (!m_hThread)
	{
		nError= NO_SYS_RESOURCE;
		return;
	}
	llStart= m_cTimer.GetStart();
	m_bError= false;
}

DWORD CChannelFTDI::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS)+sizeof(SChanInitFTDI);
	if (dwSize<sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS)+sizeof(SChanInitFTDI))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS)+sizeof(SChanInitFTDI);
	((SBaseOut*)pHead)->sBaseIn.eType= eResponseEx;
	((SBaseOut*)pHead)->sBaseIn.nChan= m_nChan;
	((SBaseOut*)pHead)->sBaseIn.nError= 0;
	((SBaseOut*)pHead)->bActive= true;
	_tcsncpy_s(((SBaseOut*)pHead)->szName, DEVICE_NAME_SIZE, m_csName.c_str(), _TRUNCATE);
	pHead= (char*)pHead+ sizeof(SBaseOut);

	((SBase*)pHead)->dwSize= sizeof(FT_DEVICE_LIST_INFO_NODE_OS)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIChan;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_FTInfo, sizeof(FT_DEVICE_LIST_INFO_NODE));
	pHead= (char*)pHead+ sizeof(FT_DEVICE_LIST_INFO_NODE_OS);

	((SBase*)pHead)->dwSize= sizeof(SChanInitFTDI)+sizeof(SBase);
	((SBase*)pHead)->eType= eFTDIChanInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sChanInit, sizeof(SChanInitFTDI));

	return sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS)+sizeof(SChanInitFTDI);
}

CChannelFTDI::~CChannelFTDI()
{
	if (m_hThread && (WAIT_OBJECT_0 != SignalObjectAndWait(m_hStop, m_hThread, 2000, FALSE)))
		TerminateThread(m_hThread, 0);
	if (m_pcComm) m_pcComm->Close();	// turn off but don't delete comm so that we can safley delete devices b/c they might call comm when closing
	for(DWORD i= 0; i<m_aDevices.size(); ++i)
		delete m_aDevices[i];
	delete m_pcComm;
	if (m_hStop) CloseHandle(m_hStop);
	if (m_hNext) CloseHandle(m_hNext);
	if (m_hUpdate) CloseHandle(m_hUpdate);
	for (size_t i= 3; i<m_ahEvents.size(); ++i)
		if (m_ahEvents[i])
			CloseHandle(m_ahEvents[i]);
	if (m_hThread) CloseHandle(m_hThread);
	lpfFT_Close(m_ftHandle);
	if (m_pcMemPool && m_aucTx) m_pcMemPool->PoolRelease(m_aucTx);
	if (m_pcMemPool && m_sRx1.aucBuff) m_pcMemPool->PoolRelease(m_sRx1.aucBuff-2);
	if (m_pcMemPool && m_sRx2.aucBuff) m_pcMemPool->PoolRelease(m_sRx2.aucBuff-2);
	DeleteCriticalSection(&m_sRx1.sSafe);
	DeleteCriticalSection(&m_sRx2.sSafe);
	delete m_pcMemPool;
}

void CChannelFTDI::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if (m_bError)
		return;
	int nError= 0;
	SBaseIn* pBase= (SBaseIn*)pHead;
	if (!pBase || dwSize < sizeof(SBaseIn) || dwSize != pBase->dwSize)	// incorrect size read
		nError= SIZE_MISSMATCH;
	else if (pBase->nChan >= (int)m_aDevices.size() || (pBase->nChan < 0 && pBase->eType != eQuery))
		nError= INVALID_CHANN;
	else if (!(((pBase->eType == eActivate || pBase->eType == eInactivate || pBase->eType == eQuery) && dwSize == sizeof(SBaseIn)) ||
		pBase->eType == ePassOn))
		nError= INVALID_COMMAND;

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
	}
	else if (pBase->eType == ePassOn)	// pass it on top device
		m_aDevices[pBase->nChan]->ProcessData((char*)pHead+sizeof(SBaseIn), dwSize-sizeof(SBaseIn), llId);
	else if (pBase->eType == eQuery)	// send back info on devices
	{
		if (pBase->nChan < 0)	// general chan info
		{
			sData.dwSize= GetInfo(NULL, 0);
			sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
			if (sData.pHead && GetInfo(sData.pHead, sData.dwSize) == sData.dwSize)
				m_pcComm->SendData(&sData, llId);

		} else	// specific chan info
		{
			sData.dwSize= m_aDevices[pBase->nChan]->GetInfo(NULL, 0);
			sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
			if (sData.pHead && m_aDevices[pBase->nChan]->GetInfo(sData.pHead, sData.dwSize) == sData.dwSize)
				m_pcComm->SendData(&sData, llId);
		}
	}
	else							// start or stop device, respond with ok
	{
		pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(sData.dwSize);
		if (pBase)
		{
			pBase->dwSize= sizeof(SBaseIn);
			pBase->eType= ((SBaseIn*)pHead)->eType;
			pBase->nChan= ((SBaseIn*)pHead)->nChan;
			pBase->nError= 0;
			sData.pHead= pBase;
			SFTUpdates sUp;
			sUp.llId= llId;
			sUp.sData= sData;
			m_asUpdates.Push(sUp);
		}
	}
}

DWORD CChannelFTDI::ThreadProc()
{
	bool bFirstRBuff= true, bUpdating= false, bNotEmpty;
	DWORD dwWrite, dwRead= 0, dwBytes, dwBaud, dwROld= 0, dwRes;
	int nError= 0;
	bool bDone= false;
	unsigned char ucMode= 0, ucOutput= 0, ucModeOld= 0, ucOutputOld;
	const DWORD dwPacket= m_FTInfo.Flags&FT_FLAGS_HISPEED ? 510 : 62;
	SetEvent(m_hUpdate);

	while (!bDone)
	{
		switch (dwRes= WaitForMultipleObjects(m_ahEvents.size(), &m_ahEvents[0], FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:	// m_hStop
			bDone= true;
			break;
		case WAIT_OBJECT_0 + 1:	// m_hUpdate, either error, something added to updates or finished updating
			if (nError)	// need to recover device, previous write would have notified of error to source
			{
				if (lpfFT_ResetPort(m_ftHandle))	// failed, try full cycle
					lpfFT_CyclePort(m_ftHandle);
				lpfFT_Close(m_ftHandle);			// either way close and open again
				if (lpfFT_OpenEx((PVOID)m_FTInfo.LocId, FT_OPEN_BY_LOCATION, &m_ftHandle))	// keep trying until canceled or success
				{
					Sleep(100);
					break;
				}
				lpfFT_SetLatencyTimer(m_ftHandle, 255);
				ucMode= 0;
				dwROld= 0;
				nError= 0;
				dwRead= 0;
				for (DWORD i= 0; i<m_aDevices.size(); ++i)
					m_aDevices[i]->DoWork(NULL, 0, m_ftHandle, eRecover, 0);
			}
			while (m_asUpdates.GetSize())	// apply user updates, i.e. activate or inactivate
			{
				SFTUpdates sUp= m_asUpdates.Front(true, bNotEmpty);
				m_aDevices[((SBaseIn*)sUp.sData.pHead)->nChan]->DoWork(NULL, 0, m_ftHandle, 
					((SBaseIn*)sUp.sData.pHead)->eType == eActivate? eActivateState: eInactivateState, 0);
				m_pcComm->SendData(&sUp.sData, sUp.llId);	// XXX this should be responded to by individual devices not here (in case not done yet)
			}
			ResetEvent(m_hUpdate);
			ResetEvent(m_hNext);
			// now setup devices
			ucModeOld= ucMode;
			ucMode= 0xFF;
			ucOutputOld= ucOutput;
			ucOutput= 0;
			dwBaud= ~0;
			dwROld= dwRead;
			dwWrite= 0;
			dwRead= 0;
			bUpdating= false;
			for (DWORD i= 0; i<m_aDevices.size(); ++i)
			{
				EStateFTDI eType= m_aDevices[i]->GetState();
				if (eType != eInactive)
				{
					ucMode &= m_aDevices[i]->m_sInitFT.ucBitMode;
					dwBaud= min(m_aDevices[i]->m_sInitFT.dwMaxBaud, dwBaud);
					dwWrite= max(m_aDevices[i]->m_sInitFT.dwMinSizeW, dwWrite);
					dwRead= max(m_aDevices[i]->m_sInitFT.dwMinSizeR, dwRead);
				}
				ucOutput |= m_aDevices[i]->m_sInitFT.ucBitOutput;
				bUpdating = bUpdating || eType == eActivateState || eType == eInactivateState;
			}
			ucMode &= (~ucMode+1);	// get lowest bit
			if (ucMode&FT_BITMODE_SYNC_BITBANG)
			{
				dwWrite= max(dwWrite, dwRead);
				dwWrite= dwPacket*(DWORD)ceil((double)dwWrite/dwPacket);	// reduce timeouts
				dwRead= dwWrite;
				if (dwRead != dwROld)
					lpfFT_SetUSBParameters(m_ftHandle, (dwRead/dwPacket)*(dwPacket+2), 0);
			}
			if (ucMode!=ucModeOld || ucOutput!=ucOutputOld)
			{
				lpfFT_Purge(m_ftHandle, FT_PURGE_RX|FT_PURGE_TX);
				lpfFT_SetBitMode(m_ftHandle, ucOutput, ucMode);
				lpfFT_SetBaudRate(m_ftHandle, dwBaud);
			}
			if (!bUpdating)	// read/write next so that we go active or inactive
				break;
			else
			{
				dwRes= WAIT_OBJECT_0 + 2;
				SetEvent(m_hNext);
			}
		default:
			if (dwRes > WAIT_OBJECT_0 + 1 && dwRes < WAIT_OBJECT_0 + m_ahEvents.size())
			{
				if (dwWrite)
				{
					for (DWORD i= 0; i<m_aDevices.size(); ++i)
						m_aDevices[i]->DoWork(m_aucTx, dwWrite, m_ftHandle, ePreWrite, 0);
					nError= FT_ERROR(lpfFT_Write(m_ftHandle, m_aucTx, dwWrite, &dwBytes), nError);
					for (DWORD i= 0; i<m_aDevices.size(); ++i)
						m_aDevices[i]->DoWork(m_aucTx, dwBytes, m_ftHandle, ePostWrite, nError);
				}
				if (dwRead && !nError)
				{
					EnterCriticalSection(bFirstRBuff?&m_sRx1.sSafe:&m_sRx2.sSafe);
					nError= FT_ERROR(lpfFT_Read(m_ftHandle, bFirstRBuff?m_sRx1.aucBuff:m_sRx2.aucBuff, dwRead, &dwBytes), nError);
					for (DWORD i= 0; i<m_aDevices.size() && !nError; ++i)
						m_aDevices[i]->DoWork(bFirstRBuff?&m_sRx1:&m_sRx2, dwBytes, m_ftHandle, ePostRead, nError);
					LeaveCriticalSection(bFirstRBuff?&m_sRx1.sSafe:&m_sRx2.sSafe);
					bFirstRBuff= !bFirstRBuff;
				}
				if (nError)
				{
					SetEvent(m_hUpdate);
					break;
				}

				// if any device can go into updating state internally remove this conditional, or give access to the updating event to that device
				if (bUpdating)
				{
					bUpdating= false;
					for (DWORD i= 0; i<m_aDevices.size(); ++i)	// are we still updating?
					{
						EStateFTDI eType= m_aDevices[i]->GetState();
						bUpdating = bUpdating || eType == eActivateState || eType == eInactivateState;
					}
					if (!bUpdating)	// done updating so go to update section so that params can be switched to non-updating state params
						SetEvent(m_hUpdate);
				}
			} else
				bDone= true;
			break;
		}
	}
	return 0;
}