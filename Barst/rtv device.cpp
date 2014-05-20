
#include "base classses.h"
#include "named pipes.h"
#include "rtv device.h"
#include "misc tools.h"
#include "AngeloRTVType_def.h"
#include "Angelo.h"

short (PASCAL *lpf_AngeloRTV_Initial)(unsigned short PortNo)= NULL;	//
short (PASCAL *lpf_AngeloRTV_Close)(unsigned short PortNo)= NULL;	//
unsigned short (PASCAL *lpf_AngeloRTV_Read_Serial)(unsigned short CardNo, unsigned long* HighByte, unsigned long* LowByte)= NULL;	//
short (PASCAL *lpf_AngeloRTV_Get_Version)(unsigned long *DriverVersion,unsigned long *DLLVersion,unsigned long *Reserved)= NULL;	//
short (PASCAL *lpf_AngeloRTV_Set_Image_Config)(unsigned short PortNo,unsigned char ConfigIndex ,unsigned char Value)= NULL;	//
short (PASCAL *lpf_AngeloRTV_Set_Color_Format)(unsigned short PortNo,unsigned char ColorFormat)= NULL;	//
short (PASCAL *lpf_AngeloRTV_Set_Video_Format)(unsigned short PortNo,unsigned char Value)= NULL;		//
short (PASCAL *lpf_AngeloRTV_Capture_Start)(unsigned short PortNo, unsigned long CaptureNo)= NULL;		//
short (PASCAL *lpf_AngeloRTV_Capture_Stop)(unsigned short PortNo)= NULL;						//
short (PASCAL *lpf_AngeloRTV_Select_Channel)(unsigned short PortNo,unsigned short Multiplex)= NULL;		//
short (PASCAL *lpf_AngeloRTV_Capture_Config)(unsigned short PortNo, unsigned long Start_Field)= NULL;	//
short (PASCAL *lpf_AngeloRTV_Set_Callback)(unsigned short PortNo, void ( __stdcall *CallBackProc)(void *VideoBufferaddress ,unsigned short PortNo))= NULL;	//
short (PASCAL *lpf_AngeloRTV_Get_Int_Status)(unsigned short PortNo,unsigned long *IntStatus)= NULL;	//

static CManagerRTV* s_pCRTVMan= NULL;

extern "C"  void __stdcall MediaStreamProc(void* VideoBufferAddress ,unsigned short PortNo)
{
	DWORD dwStatus;
	if (s_pCRTVMan && !lpf_AngeloRTV_Get_Int_Status(PortNo, &dwStatus) && (dwStatus&~0x01) == 0x02)
		s_pCRTVMan->NextFrame((unsigned char*)VideoBufferAddress, PortNo);
}


CManagerRTV::CManagerRTV(CComm* pcComm, const TCHAR szPipe[], int nChan, int &nError) : 
	CManager(RTV_MAN_STR, std::tstring(szPipe), nChan)
{
	nError= 0;
	m_bError= true;
	m_pcComm= NULL;
	m_pcLogBuffer= NULL;
	m_pcMemPool= new CMemPool;
	m_hLib= NULL;
	m_usChans= 0;

	if(s_pCRTVMan)
	{
		nError= ALREADY_OPEN;
		return;
	}
	s_pCRTVMan= this;
	if (!pcComm || !szPipe)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}
	BOOL bRes= GetProcAddresses(&m_hLib, _T("AngeloRTV.dll"), 13, &lpf_AngeloRTV_Initial, "AngeloRTV_Initial", 
		&lpf_AngeloRTV_Close, "AngeloRTV_Close", &lpf_AngeloRTV_Read_Serial, "AngeloRTV_Read_Serial", 
		&lpf_AngeloRTV_Get_Version, "AngeloRTV_Get_Version", &lpf_AngeloRTV_Set_Image_Config, "AngeloRTV_Set_Image_Config",
		&lpf_AngeloRTV_Set_Color_Format, "AngeloRTV_Set_Color_Format", &lpf_AngeloRTV_Set_Video_Format, 
		"AngeloRTV_Set_Video_Format", &lpf_AngeloRTV_Capture_Start, "AngeloRTV_Capture_Start", &lpf_AngeloRTV_Capture_Stop, 
		"AngeloRTV_Capture_Stop", &lpf_AngeloRTV_Select_Channel, "AngeloRTV_Select_Channel", &lpf_AngeloRTV_Capture_Config, 
		"AngeloRTV_Capture_Config", &lpf_AngeloRTV_Set_Callback, "AngeloRTV_Set_Callback", &lpf_AngeloRTV_Get_Int_Status, 
		"AngeloRTV_Get_Int_Status");
	if (!bRes || !m_hLib)
	{
		nError= DRIVER_ERROR;
		return;
	}
	DWORD dwDriverVersion[4]= {0};
	DWORD dwDLLVersion[4]= {0};
	DWORD dwReserved[4]= {0};
	if (nError= RTV_ERROR(lpf_AngeloRTV_Get_Version(dwDriverVersion, dwDLLVersion, dwReserved), nError))
		return;
	if (dwDLLVersion[0]*1000+dwDLLVersion[1]*100+dwDLLVersion[2]*10+dwDLLVersion[3] < MIN_RTV_LIB_VER)
	{
		nError= DRIVER_ERROR;
		return;
	}
	unsigned short i= 0;
	while (!lpf_AngeloRTV_Initial(i))	// find out how many cards total we have
	{
		i+= 4;
	}
	m_usChans= i;	// total ports
	m_ahCallbackSafe.assign(m_usChans, NULL);
	m_acRTVDevices.assign(m_usChans, NULL);
	for (i= 0; i<m_usChans; ++i)
	{
		m_ahCallbackSafe[i]= new CRITICAL_SECTION;
		InitializeCriticalSection(m_ahCallbackSafe[i]);
	}
	m_bError= false;
	m_pcComm= pcComm;
}

DWORD CManagerRTV::GetInfo(void* pHead, DWORD dwSize)
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

CManagerRTV::~CManagerRTV()
{
	for (size_t i= 0; i<m_usChans; ++i)
		delete m_acRTVDevices[i];
	if (m_hLib != NULL)
        FreeLibrary(m_hLib);
	delete m_pcMemPool;
	for (unsigned short i= 0; i<m_usChans; ++i)
	{
		DeleteCriticalSection(m_ahCallbackSafe[i]);
		delete m_ahCallbackSafe[i];
	}
	s_pCRTVMan= NULL;
}

void CManagerRTV::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
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
		if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acRTVDevices.size() || 
			!m_acRTVDevices[((SBaseIn*)pHead)->nChan])	// invalid channel
		{
			sBase.nError= INVALID_CHANN;
		} else			// send info on particular chann
		{
			bRes= false;
			DWORD dwSizeInfo= m_acRTVDevices[((SBaseIn*)pHead)->nChan]->GetInfo(NULL, 0);
			pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(dwSizeInfo);
			if (pBase)
			{
				m_acRTVDevices[((SBaseIn*)pHead)->nChan]->GetInfo(pBase, dwSizeInfo);
				sData.dwSize= dwSizeInfo;
				sData.pHead= pBase;
				m_pcComm->SendData(&sData, llId);
			}
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eDelete)	// delete a channel
	{
		sBase.eType= eDelete;
		if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acRTVDevices.size() || 
			!m_acRTVDevices[((SBaseIn*)pHead)->nChan])
			sBase.nError= INVALID_CHANN;
		else
		{
			EnterCriticalSection(m_ahCallbackSafe[((SBaseIn*)pHead)->nChan]);
			delete m_acRTVDevices[((SBaseIn*)pHead)->nChan];
			m_acRTVDevices[((SBaseIn*)pHead)->nChan]= NULL;
			LeaveCriticalSection(m_ahCallbackSafe[((SBaseIn*)pHead)->nChan]);
			sBase.nChan= ((SBaseIn*)pHead)->nChan;
		}
	} else if (dwSize == sizeof(SBaseIn) && ((SBaseIn*)pHead)->eType == eVersion && 
		((SBaseIn*)pHead)->nChan == -1)
	{
		DWORD dw1[4]= {0}, dw2[4]= {0}, dwDLLVersion[4]= {0};
		sBase.nError= RTV_ERROR(lpf_AngeloRTV_Get_Version(dw1, dwDLLVersion, dw2), sBase.nError);
		sBase.dwInfo= dwDLLVersion[0]*1000+dwDLLVersion[1]*100+dwDLLVersion[2]*10+dwDLLVersion[3];
		sBase.eType= eVersion;
	} else if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acRTVDevices.size())
	{
		sBase.eType= ((SBaseIn*)pHead)->eType;
		sBase.nError= INVALID_CHANN;
	} else if (((SBaseIn*)pHead)->eType == eSet && m_acRTVDevices[((SBaseIn*)pHead)->nChan])
	{
		sBase.eType= ((SBaseIn*)pHead)->eType;
		sBase.nError= ALREADY_OPEN;
	} else if (((SBaseIn*)pHead)->eType == eSet && 
		((SBaseIn*)pHead)->dwSize == sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitRTV) && 
		((SBase*)((char*)pHead+sizeof(SBaseIn)))->eType == eRTVChanInit)	// set a channel
	{
		bRes= false;
		LARGE_INTEGER llStart;
		sBase.eType= eSet;	// in case of error we do respond at end
		SChanInitRTV sChanInit= *(SChanInitRTV*)((char*)pHead+sizeof(SBase)+sizeof(SBaseIn));
		int		nWidth;	// Image height
		int		nHeight;// Image width
		unsigned char	ucBpp;	// Image depth (bytes per pixel)
		// Calculate frame size from the video format selected
		switch (sChanInit.ucVideoFmt)
		{
		case 0:	//Full NTSC
			nWidth= 640;
			nHeight= 480;
			break;
		case 1:	//Full PAL
			nWidth= 768;
			nHeight= 576;
			break;
		case 2:	//CIF NTSC
			nWidth= 320;
			nHeight= 240;
			break;
		case 3:	//CIF PAL
			nWidth= 384;
			nHeight= 288;
			break;
		case 4:	//QCIF NTSC
			nWidth= 160;
			nHeight= 120;
			break;
		case 5: //QCIF PAL
			nWidth= 192;
			nHeight= 144;
			break;
		default:	// Other case, use full NTSC
			sBase.nError= BAD_INPUT_PARAMS;
			break;
		}

		// Calculate the image depth and output format
		switch (sChanInit.ucColorFmt)
		{
		case 0:	// RGB16, PIX_FMT_RGB565LE
			ucBpp= 2;
			break;
		case 2:	// RGB15, PIX_FMT_RGB555LE
			ucBpp= 2;
			break;
		case 1:	// Gray, PIX_FMT_GRAY8
			ucBpp= 1;
			break;
		case 5:	// RGB8, PIX_FMT_RGB8
			ucBpp= 1;
			break;
		case 6:	// RAW8X
			ucBpp= 1;
		//	sEncodeParams.ePixelFmtIn= (PixelFormat)0;
			break;
		case 7:	// YUY2, PIX_FMT_YUYV422, PIX_FMT_YUV422P
			ucBpp= 2;
			break;
		case 8:	// btYUV
			ucBpp= 1;
		//	sEncodeParams.ePixelFmtIn= (PixelFormat)0;
			break;
		case 3:	// RGB24, PIX_FMT_BGR24
			ucBpp= 3;
			break;
		case 4:	// RGB32, PIX_FMT_BGRA
			ucBpp= 4;
			break;
		default:	// use PIX_FMT_GRAY8
			sBase.nError= BAD_INPUT_PARAMS;
			break;
		}
		sChanInit.nHeight= nHeight;
		sChanInit.nWidth= nWidth;
		sChanInit.ucBpp= ucBpp;
		sChanInit.dwBuffSize= nHeight*nWidth*ucBpp;
		std::tstringstream ss;	// rtv channel
		ss<<((SBaseIn*)pHead)->nChan;
		std::tstringstream ss2;	// rtv manager index
		ss2<<m_nChan;
		std::tstring csPipeName= m_csPipeName+_T(":")+ss2.str()+_T(":")+ss.str(); // new channel pipe name
		CChannelRTV* pcChan= NULL;
		if (!sBase.nError)
			pcChan= new CChannelRTV(csPipeName.c_str(), ((SBaseIn*)pHead)->nChan, sChanInit, sBase.nError, llStart);
		if (!sBase.nError)
		{
			EnterCriticalSection(m_ahCallbackSafe[((SBaseIn*)pHead)->nChan]);
			m_acRTVDevices[((SBaseIn*)pHead)->nChan]= pcChan;
			LeaveCriticalSection(m_ahCallbackSafe[((SBaseIn*)pHead)->nChan]);
			SBaseOut* pBaseO= (SBaseOut*)m_pcMemPool->PoolAcquire(sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV));
			if (pBaseO)
			{
				pBaseO->sBaseIn.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV);
				pBaseO->sBaseIn.eType= eResponseExL;
				pBaseO->sBaseIn.nChan= ((SBaseIn*)pHead)->nChan;
				pBaseO->sBaseIn.nError= 0;
				pBaseO->llLargeInteger= llStart;
				pBaseO->bActive= false;
				((SBase*)((char*)pBaseO+sizeof(SBaseOut)))->dwSize= sizeof(SBase)+sizeof(SChanInitRTV);
				((SBase*)((char*)pBaseO+sizeof(SBaseOut)))->eType= eRTVChanInit;
				*((SChanInitRTV*)((char*)pBaseO+sizeof(SBaseOut)+sizeof(SBase)))= sChanInit;
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

void CManagerRTV::NextFrame(unsigned char* aucData, unsigned short usChan)
{
	if (usChan < m_usChans)
	{
		EnterCriticalSection(m_ahCallbackSafe[usChan]);	// make sure devices isn't changed while we're working on it
		if (m_acRTVDevices[usChan])
			m_acRTVDevices[usChan]->NextFrame(aucData);
		LeaveCriticalSection(m_ahCallbackSafe[usChan]);
	}
}





CChannelRTV::CChannelRTV(const TCHAR szPipe[], int nChan, SChanInitRTV &sChanInit, int &nError, 
		LARGE_INTEGER &llStart) : CDevice(RTV_CHAN_STR), m_csPipeName(szPipe), 
		m_sChanInit(sChanInit), m_usChan(nChan)
{
	m_bError= true;
	m_pcComm= NULL;
	m_bSent= false;
	m_pLastSent= NULL;
	m_bActive= false;
	m_pcMemPool= new CMemPool;
	InitializeCriticalSection(&m_hSentSafe);
	nError= 0;
	if (!szPipe)
	{
		nError= BAD_INPUT_PARAMS;
		return;
	}

	lpf_AngeloRTV_Close(m_usChan);
	nError= RTV_ERROR(lpf_AngeloRTV_Initial(m_usChan), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Video_Format(m_usChan, sChanInit.ucVideoFmt), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Color_Format(m_usChan, sChanInit.ucColorFmt), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Image_Config(m_usChan, 0, sChanInit.ucBrightness), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Image_Config(m_usChan, 1, sChanInit.ucHue), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Image_Config(m_usChan, 2, sChanInit.ucUSat), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Image_Config(m_usChan, 3, sChanInit.ucVSat), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Image_Config(m_usChan, 4, sChanInit.ucLumaContrast), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Image_Config(m_usChan, 5, sChanInit.ucLumaFilt), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Select_Channel(m_usChan, 0x01), nError);
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Capture_Config(m_usChan, 0), nError);	// Odd field + Even field
	if (!nError) nError= RTV_ERROR(lpf_AngeloRTV_Set_Callback(m_usChan, MediaStreamProc), nError);
	if (nError)
		return;

	m_pcComm= new CPipeServer;	// our pipe over which comm to devices will occur
	nError= static_cast<CPipeServer*>(m_pcComm)->Init(szPipe, ~0x80000000, MIN_BUFF_IN, 
		sChanInit.dwBuffSize+MIN_BUFF_OUT, this, NULL);
	if (nError)
		return;
	llStart= m_cTimer.GetStart();
	m_bError= false;
}

DWORD CChannelRTV::GetInfo(void* pHead, DWORD dwSize)
{
	if (!pHead)
		return sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV);
	if (dwSize<sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV))
		return 0;

	((SBaseOut*)pHead)->sBaseIn.dwSize= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV);
	((SBaseOut*)pHead)->sBaseIn.eType= eResponseEx;
	((SBaseOut*)pHead)->sBaseIn.nChan= m_usChan;
	((SBaseOut*)pHead)->sBaseIn.nError= 0;
	EnterCriticalSection(&m_hSentSafe);
	((SBaseOut*)pHead)->bActive= m_bActive;
	LeaveCriticalSection(&m_hSentSafe);
	_tcsncpy_s(((SBaseOut*)pHead)->szName, DEVICE_NAME_SIZE, m_csName.c_str(), _TRUNCATE);
	pHead= (char*)pHead+ sizeof(SBaseOut);

	((SBase*)pHead)->dwSize= sizeof(SChanInitRTV)+sizeof(SBase);
	((SBase*)pHead)->eType= eRTVChanInit;
	pHead= (char*)pHead+ sizeof(SBase);
	memcpy(pHead, &m_sChanInit, sizeof(SChanInitRTV));

	return sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV);
}

CChannelRTV::~CChannelRTV()
{
	lpf_AngeloRTV_Capture_Stop(m_usChan);
	lpf_AngeloRTV_Set_Callback(m_usChan, NULL);
	lpf_AngeloRTV_Close(m_usChan);
	if (m_pcComm) m_pcComm->Close();
	delete m_pcComm;
	DeleteCriticalSection(&m_hSentSafe);
	delete m_pcMemPool;
}

void CChannelRTV::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if (m_bError)
		return;
	int nError= 0;
	SBaseIn* pBase= (SBaseIn*)pHead;
	if (!pBase || dwSize != sizeof(SBaseIn) || dwSize != pBase->dwSize)	// incorrect size read
		nError= SIZE_MISSMATCH;
	else if (pBase->eType != eActivate && pBase->eType != eInactivate && pBase->eType != eQuery)
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
	} else if (pBase->eType == eQuery)	// send back info on devices
	{
		sData.dwSize= GetInfo(NULL, 0);
		sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead && GetInfo(sData.pHead, sData.dwSize) == sData.dwSize)
			m_pcComm->SendData(&sData, llId);
	} else							// start or stop device, respond with ok
	{
		EnterCriticalSection(&m_hSentSafe);
		if (pBase->eType == eActivate)
		{
			if (m_bActive)
				nError= ALREADY_OPEN;
			else
			{
				nError= RTV_ERROR(lpf_AngeloRTV_Capture_Start(m_usChan, 0xFFFFFFFF), nError);
				if (!nError)
				{
					m_bActive= true;
					m_llId= llId;
				}
			}
		} else
		{
			lpf_AngeloRTV_Capture_Stop(m_usChan);
			m_bActive= false;
			m_llId= -1;
			m_bSent= false;
			m_pLastSent= NULL;
		}
		pBase= (SBaseIn*)m_pcMemPool->PoolAcquire(sData.dwSize);
		if (pBase)
		{
			pBase->dwSize= sizeof(SBaseIn);
			pBase->eType= ((SBaseIn*)pHead)->eType;
			pBase->nChan= ((SBaseIn*)pHead)->nChan;
			pBase->nError= nError;
			sData.pHead= pBase;
			m_pcComm->SendData(&sData, llId);
		}
		LeaveCriticalSection(&m_hSentSafe);	// don't exit earlier so this massege is first response
	}
}

void CChannelRTV::NextFrame(unsigned char* aucData)
{
	SData sData;
	sData.pHead= NULL;
	EnterCriticalSection(&m_hSentSafe);
	if(m_bActive && !m_bSent)
	{
		sData.pHead= m_pcMemPool->PoolAcquire(sizeof(SBaseOut)+sizeof(SBase)+m_sChanInit.dwBuffSize);
		if (!m_sChanInit.bLossless && sData.pHead)
		{
			m_bSent= true;
			m_pLastSent= sData.pHead;
		}
	}
	LeaveCriticalSection(&m_hSentSafe);

	if (!sData.pHead)
		return;
	sData.pDevice= this;
	sData.dwSize= sizeof(SBaseOut)+sizeof(SBase)+m_sChanInit.dwBuffSize;
	((SBaseOut*)sData.pHead)->sBaseIn.dwSize= sData.dwSize;
	((SBaseOut*)sData.pHead)->sBaseIn.eType= eResponseExD;
	((SBaseOut*)sData.pHead)->sBaseIn.nChan= m_usChan;
	((SBaseOut*)sData.pHead)->sBaseIn.nError= 0;
	((SBaseOut*)sData.pHead)->bActive= true;
	((SBaseOut*)sData.pHead)->dDouble= m_cTimer.Seconds();
	((SBase*)((char*)sData.pHead+sizeof(SBaseOut)))->dwSize= sizeof(SBase)+m_sChanInit.dwBuffSize;
	((SBase*)((char*)sData.pHead+sizeof(SBaseOut)))->eType= eRTVImageBuf;
	memcpy((char*)sData.pHead+sizeof(SBaseOut)+sizeof(SBase), aucData, m_sChanInit.dwBuffSize);
	m_pcComm->SendData(&sData, m_llId);
}

void CChannelRTV::Result(void *pHead, bool bPass)
{
	EnterCriticalSection(&m_hSentSafe);
	if(m_bSent && m_pLastSent == pHead)
	{
		m_bSent= false;
		m_pLastSent= NULL;
	}
	LeaveCriticalSection(&m_hSentSafe);
	m_pcMemPool->PoolRelease(pHead);
}