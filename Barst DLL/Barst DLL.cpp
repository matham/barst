// Barst DLL.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "Barst DLL.h"
#include <Psapi.h>
#include <sstream>
#include "misc tools.h"


static CTimer s_cTimer;


inline int BarstOpenPipe(const TCHAR* szPipe, DWORD dwAccess, DWORD dwTimeout, HANDLE* phPipe)
{
	if (!szPipe || !phPipe)
		return BAD_INPUT_PARAMS;
	int nRes;
	*phPipe= CreateFile(szPipe, dwAccess, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (*phPipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && dwTimeout)
	{
		if(!WaitNamedPipe(szPipe, dwTimeout))
			return WIN_ERROR(GetLastError(), nRes);
		else	// try again
		{
			*phPipe= CreateFile(szPipe, dwAccess, 0, NULL, OPEN_EXISTING, 0, NULL);
			if (*phPipe == INVALID_HANDLE_VALUE)
				return WIN_ERROR(GetLastError(), nRes);
		}
	} else if (*phPipe == INVALID_HANDLE_VALUE)
		return WIN_ERROR(GetLastError(), nRes);
	DWORD dwMode= PIPE_READMODE_MESSAGE | PIPE_WAIT;
	if (!SetNamedPipeHandleState(*phPipe, &dwMode, NULL, NULL))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		CloseHandle(*phPipe);
		return nRes;
	}
	return 0;
}

inline int BarstWriteRead(HANDLE hPipe, DWORD dwWrite, void* pWrite, DWORD* pdwRead, void* pRead)
{
	int nRes;
	DWORD dwBytes;
	if (!WriteFile(hPipe, pWrite, dwWrite, &dwBytes, NULL) || dwBytes != dwWrite)
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return nRes;
	}
	if (pRead && !ReadFile(hPipe, pRead, *pdwRead, pdwRead, NULL))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		return nRes;
	}
	return 0;
}

extern "C"	BARSTDLL_API int __stdcall BarstStart(const TCHAR* szPath, const TCHAR* szCurrDir, const TCHAR* szPipe, 
	DWORD dwBuffSizeWrite, DWORD dwBuffSizeRead, DWORD dwTimeout)
{
	if (!szPath || !szPipe || !szCurrDir)
		return BAD_INPUT_PARAMS;
	std::tstring csFilename(szPath);
	if (csFilename.find_last_of(_T("\\")) == -1)	// strip the name of the exe to launch from full path
		return BAD_INPUT_PARAMS;
	csFilename= csFilename.substr(csFilename.find_last_of(_T("\\"))+1);	// "Barst.exe"

	HANDLE hPipe;	// check if this pipe/exe already exists
	if ((hPipe= CreateFile(szPipe, GENERIC_WRITE|GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL)) != INVALID_HANDLE_VALUE || 
		GetLastError() == ERROR_PIPE_BUSY)
	{
		if (hPipe != INVALID_HANDLE_VALUE)
			CloseHandle(hPipe);
		return ALREADY_OPEN;
	}
	if (dwBuffSizeWrite<MIN_BUFF_IN)
		dwBuffSizeWrite= MIN_BUFF_IN;
	if (dwBuffSizeRead<MIN_BUFF_OUT)
		dwBuffSizeRead= MIN_BUFF_OUT;
	int nRes;
	std::tstringstream ss;	// params to pass to exe
	ss<<_T("\"")<<csFilename<<_T("\" \"")<<szPipe<<_T("\" ")<<dwBuffSizeWrite<<_T(" ")<<dwBuffSizeRead;

	TCHAR* szParams= new TCHAR[ss.str().length()+1];
	_tcscpy_s(szParams, ss.str().length()+1, ss.str().c_str());
	STARTUPINFO sStart;
	memset(&sStart, 0, sizeof(STARTUPINFO));
	sStart.cb= sizeof(STARTUPINFO);
	sStart.dwFlags= STARTF_USESHOWWINDOW;
	sStart.wShowWindow= SW_HIDE;
	PROCESS_INFORMATION sProcess;
	memset(&sProcess, 0, sizeof(PROCESS_INFORMATION));

	//launch Barst
	if (!(nRes= WIN_ERROR(CreateProcess(szPath, szParams, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, szCurrDir, &sStart, &sProcess), nRes)))
		return nRes;
	CloseHandle(sProcess.hProcess);
	CloseHandle(sProcess.hThread);

	// verify that it's open
	DWORD dwVersion;
	DWORD dwS= GetTickCount();
	while (GetTickCount()-dwS < dwTimeout)
	{
		if (!BarstGetVersion(szPipe, 0, &dwVersion))
			return 0;
	}
	return TIMED_OUT;
}

extern "C"	BARSTDLL_API int __stdcall BarstEnd(const TCHAR* szPipe, DWORD dwTimeout)
{
	if (!szPipe)
		return BAD_INPUT_PARAMS;
	HANDLE hPipe;
	int nRes= BarstOpenPipe(szPipe, GENERIC_WRITE, dwTimeout, &hPipe);
	if (nRes)
		return nRes;

	SBaseIn sBase;
	sBase.dwSize= sizeof(SBaseIn);
	sBase.eType= eDelete;
	sBase.nChan= -1;	// -1 tells barst to close
	sBase.nError= 0;
	nRes= BarstWriteRead(hPipe, sizeof(SBaseIn), &sBase, NULL, NULL);	// tell barst to close
	CloseHandle(hPipe);
	if (nRes)
		return nRes;

	DWORD dwVersion;	// verify that it's closed
	DWORD dwS= GetTickCount();
	while (GetTickCount()-dwS < dwTimeout)
	{
		if (BarstGetVersion(szPipe, 0, &dwVersion))
			return 0;
	}
	return TIMED_OUT;
}

extern "C"	BARSTDLL_API int __stdcall BarstOpenHandle(const TCHAR* szPipe, DWORD dwTimeout, HANDLE* phPipe)
{
	return BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, phPipe);
}

extern "C"	BARSTDLL_API void __stdcall BarstCloseHandle(HANDLE hPipe)
{
	if (hPipe != INVALID_HANDLE_VALUE && hPipe != NULL)
		CloseHandle(hPipe);
}

extern "C"	BARSTDLL_API int __stdcall BarstGetVersion(const TCHAR* szPipe, DWORD dwTimeout, DWORD* pdwVersion)
{
	if (!pdwVersion)
		return BAD_INPUT_PARAMS;
	int nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);	// pipe
	if (nRes)
		return nRes;
	
	SBaseIn sBase;
	sBase.dwSize= sizeof(SBaseIn);
	sBase.eType= eVersion;
	sBase.nChan= -1;
	sBase.nError= 0;
	SBaseIn* pBase= (SBaseIn*)malloc(MIN_BUFF_OUT);
	if (!pBase)
	{
		CloseHandle(hPipe);
		return NO_SYS_RESOURCE;
	}
	DWORD dwRead= MIN_BUFF_OUT;

	nRes= BarstWriteRead(hPipe, sizeof(SBaseIn), &sBase, &dwRead, pBase);	// request version
	if (!nRes)
	{
		if (dwRead == sizeof(SBaseIn) && !pBase->nError && pBase->eType == eVersion)
			*pdwVersion= pBase->dwInfo;
		else if (dwRead == sizeof(SBaseIn) && pBase->nError)
			nRes= pBase->nError;
		else
			nRes= UNEXPECTED_READ;
	}

	free(pBase);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstGetManVersion(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan, DWORD* pdwVersion)
{
	if (!pdwVersion)
		return BAD_INPUT_PARAMS;
	int nManChan;
	int nRes= BarstCreateManager(szPipe, dwTimeout, eMan, &nManChan);
	if (nRes==ALREADY_OPEN)
		nRes= 0;
	if (nRes)
		return nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);	// pipe
	if (nRes)
		return nRes;
	
	SBaseIn* pBaseWrite= (SBaseIn*)malloc(2*sizeof(SBaseIn));
	SBaseIn* pBase= (SBaseIn*)malloc(sizeof(SBaseIn));
	DWORD dwRead= sizeof(SBaseIn);
	if (!pBase || !pBaseWrite)
	{
		CloseHandle(hPipe);
		free(pBaseWrite);
		free(pBase);
		return NO_SYS_RESOURCE;
	}
	pBaseWrite->dwSize= 2*sizeof(SBaseIn);
	pBaseWrite->eType= ePassOn;
	pBaseWrite->nChan= nManChan;
	pBaseWrite->nError= 0;
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->dwSize= sizeof(SBaseIn);
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->eType= eVersion;
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->nChan= -1;
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->nError= 0;
	nRes= BarstWriteRead(hPipe, 2*sizeof(SBaseIn), pBaseWrite, &dwRead, pBase);	// request version
	if (!nRes)
	{
		if (dwRead == sizeof(SBaseIn) && !pBase->nError && pBase->eType == eVersion)
			*pdwVersion= pBase->dwInfo;
		else if (dwRead == sizeof(SBaseIn) && pBase->nError)
			nRes= pBase->nError;
		else
			nRes= UNEXPECTED_READ;
	}

	free(pBaseWrite);
	free(pBase);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstGetManDeviceInfo(const TCHAR* szPipe, DWORD dwTimeout, int nChan, SBaseOut* psInfo)
{
	if (!szPipe || !psInfo)
		return BAD_INPUT_PARAMS;
	HANDLE hPipe;
	int nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);	// pipe
	if (nRes)
		return nRes;

	SBaseIn sBase;
	sBase.dwSize= sizeof(SBaseIn);
	sBase.eType= eQuery;
	sBase.nChan= nChan;
	sBase.nError= 0;
	SBaseOut* pBase= (SBaseOut*)malloc(MIN_BUFF_IN);
	if (!pBase)
	{
		CloseHandle(hPipe);
		return NO_SYS_RESOURCE;
	}
	DWORD dwRead= MIN_BUFF_IN;
	nRes= BarstWriteRead(hPipe, sizeof(SBaseIn), &sBase, &dwRead, pBase);	// get response
	if (!nRes)
	{
		if (dwRead >= sizeof(SBaseOut) && pBase->sBaseIn.eType == eResponseEx && !pBase->sBaseIn.nError)	// query genarlly sends eResponseEx b/c it sends internal name
			memcpy(psInfo, pBase, sizeof(SBaseOut));
		else if ((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && pBase->sBaseIn.nError)
			nRes= pBase->sBaseIn.nError;
		else
			nRes= UNEXPECTED_READ;
	}

	free(pBase);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstCreateManager(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan, int* pnChan)
{
	if (!szPipe || !pnChan)
		return BAD_INPUT_PARAMS;
	HANDLE hPipe;
	int nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);	// pipe
	if (nRes)
		return nRes;

	SBaseIn sBase;
	sBase.dwSize= sizeof(SBaseIn);
	sBase.eType= eSet;
	sBase.eType2= eMan;
	sBase.nError= 0;
	SBaseIn* pBase= (SBaseIn*)malloc(MIN_BUFF_IN);
	if (!pBase)
	{
		CloseHandle(hPipe);
		return NO_SYS_RESOURCE;
	}
	DWORD dwRead= MIN_BUFF_IN;
	nRes= BarstWriteRead(hPipe, sizeof(SBaseIn), &sBase, &dwRead, pBase);	// get response
	if (!nRes)
	{
		if (dwRead == sizeof(SBaseIn))
		{
			*pnChan= pBase->nChan;
			nRes= pBase->nError;
		}
		else
			nRes= UNEXPECTED_READ;
	}

	free(pBase);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstCloseManager(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan)
{
	if (!szPipe)
		return BAD_INPUT_PARAMS;
	int nManChan;	// get manager
	int nRes= BarstCreateManager(szPipe, dwTimeout, eMan, &nManChan);
	if (nRes==ALREADY_OPEN)
		nRes= 0;
	if (nRes)
		return nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);
	if(nRes)
		return nRes;

	SBaseIn* pBase= (SBaseIn*)malloc(sizeof(SBaseIn));
	void* pHeadIn= malloc(sizeof(SBaseIn));
	if (!pBase || !pHeadIn)
	{
		CloseHandle(hPipe);
		free(pBase);
		free(pHeadIn);
		return NO_SYS_RESOURCE;
	}
	pBase->dwSize= sizeof(SBaseIn);
	pBase->eType= eDelete;
	pBase->nChan= nManChan;
	pBase->nError= 0;
	DWORD dwRead= sizeof(SBaseIn);
	nRes= BarstWriteRead(hPipe, sizeof(SBaseIn), pBase, &dwRead, pHeadIn);
	if (!nRes)
	{
		if (dwRead != sizeof(SBaseIn))
			nRes= UNEXPECTED_READ;
		else
			nRes= ((SBaseIn*)pHeadIn)->nError;
	}

	free(pHeadIn);
	free(pBase);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstCreateFTDIChan(const TCHAR* szPipe, DWORD dwTimeout, const char* szDesc, const char* szSerial, 
	const SChanInitFTDI* psChanInit, void* pChanData, DWORD dwSize, int* pnManChan, int* pnChan, FT_DEVICE_LIST_INFO_NODE_OS* psFTInfo, 
	LARGE_INTEGER *pllStart)
{
	if (!szPipe || !psChanInit || !pChanData || !dwSize || !pnChan || !pnManChan || !psFTInfo || !pllStart)
		return BAD_INPUT_PARAMS;

	// open ftdi manager
	int nRes= BarstCreateManager(szPipe, dwTimeout, eFTDIMan, pnManChan);
	if (nRes==ALREADY_OPEN)
		nRes= 0;
	if (nRes)
		return nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);
	if(nRes)
		return nRes;

	DWORD dwRead= 25*(sizeof(SBaseOut)+2*sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS)+sizeof(SChanInitFTDI)); //max 25 devices
	void* pHeadOut= malloc(2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitFTDI)+dwSize);	// for later
	void* pHeadIn= malloc(dwRead);
	if (!pHeadOut || !pHeadIn)
	{
		CloseHandle(hPipe);
		free(pHeadOut);
		free(pHeadIn);
		return NO_SYS_RESOURCE;
	}
	SBaseIn* pBase= (SBaseIn*)pHeadOut;
	pBase->dwSize= 2*sizeof(SBaseIn);
	pBase->eType= ePassOn;	// pass on to ftdi manager
	pBase->nChan= *pnManChan;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SBaseIn);
	pBase->eType= eQuery;
	pBase->nChan= -1;	// request info on all the USB connected devices
	pBase->nError= 0;
	nRes= BarstWriteRead(hPipe, 2*sizeof(SBaseIn), pHeadOut, &dwRead, pHeadIn);
	if (!nRes)
	{
		if (dwRead == sizeof(SBaseIn) && ((SBaseIn*)pHeadIn)->dwSize == sizeof(SBaseIn) &&
			((SBaseIn*)pHeadIn)->nError)
			nRes= ((SBaseIn*)pHeadIn)->nError;
		else if (dwRead < sizeof(SBaseIn)+sizeof(SBaseOut)+sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS))
			nRes= NO_CHAN;
	}
	if (nRes)	// there's no FTDI channel
	{
		CloseHandle(hPipe);
		free(pHeadOut);
		free(pHeadIn);
		return nRes;
	}
	// now we should have list of devices, each having a SBaseOut struct followed by SBase followed by FT_DEVICE_LIST_INFO_NODE
	// (and followed by SBase and SChanInitFTDI if the channel is open in the manager). We know if it's open because opened channels
	// have bActive true in SBaseOut (and nChan != -1). If a channel isn't open we can open it after this call by using the location 
	// in the list in which it occured. If the channel is open, we can address it only by its channel number. Note, the list number 
	// for unopened channels are valid only until the next query call.
	SBaseOut* pBaseO= (SBaseOut*)((char*)pHeadIn+sizeof(SBaseIn));	// first device
	FT_DEVICE_LIST_INFO_NODE_OS* pFT;
	int i= 0, nChan= -1;	// i is position in list
	bool bFound= false;
	for (; (char*)pBaseO < (char*)pHeadIn+dwRead; ++i)
	{
		if (pBaseO->sBaseIn.dwSize >= sizeof(SBaseOut)+sizeof(SBase)+sizeof(FT_DEVICE_LIST_INFO_NODE_OS) && pBaseO->sBaseIn.eType == eResponseEx &&
			((SBase*)((char*)pBaseO+sizeof(SBaseOut)))->eType == eFTDIChan)
		{
			pFT= (FT_DEVICE_LIST_INFO_NODE_OS*)((char*)pBaseO+sizeof(SBaseOut)+sizeof(SBase));
			bFound= szDesc && !strcmp(pFT->Description, szDesc);
			bFound= bFound || (szSerial && !strcmp(pFT->SerialNumber, szSerial));
			if (bFound)
			{
				*psFTInfo= *pFT;
				nChan= pBaseO->sBaseIn.nChan;
				break;
			}
			pBaseO= (SBaseOut*)((char*)pBaseO+pBaseO->sBaseIn.dwSize);
		} else
		{
			nRes= UNEXPECTED_READ;
			break;
		}
	}
	if (!bFound)	// didn't find matching device
		nRes= NO_CHAN;
	if (nChan >= 0)	// channel was already open
	{
		*pnChan= nChan;
		nRes= ALREADY_OPEN;
	}
	if (nRes)
	{
		CloseHandle(hPipe);
		free(pHeadIn);
		free(pHeadOut);
		return nRes;
	}
	// now we need to open the device
	pBase= (SBaseIn*)pHeadOut;
	pBase->dwSize= 2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitFTDI)+dwSize;
	pBase->eType= ePassOn;
	pBase->nChan= *pnManChan;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitFTDI)+dwSize;
	pBase->eType= eSet;
	pBase->nChan= i;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SChanInitFTDI)+sizeof(SBase);	// SBase*
	pBase->eType= eFTDIChanInit;
	SChanInitFTDI* pInit= (SChanInitFTDI*)((char*)pBase+sizeof(SBase));
	memcpy(pInit, psChanInit, sizeof(SChanInitFTDI));
	pInit->dwBuffIn= max(MIN_BUFF_OUT, psChanInit->dwBuffIn);
	pInit->dwBuffOut= max(MIN_BUFF_IN, psChanInit->dwBuffOut);
	++pInit;
	memcpy(pInit, pChanData, dwSize);

	dwRead= sizeof(SBaseOut);
	nRes= BarstWriteRead(hPipe, 2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitFTDI)+dwSize, 
		pHeadOut, &dwRead, pHeadIn);
	if (!nRes)
	{
		if ((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && ((SBaseIn*)pHeadIn)->dwSize == dwRead &&
			((SBaseIn*)pHeadIn)->nError)
			nRes= ((SBaseIn*)pHeadIn)->nError;
		else if (dwRead != sizeof(SBaseOut) || ((SBaseIn*)pHeadIn)->dwSize != sizeof(SBaseOut) ||
			((SBaseIn*)pHeadIn)->eType != eResponseExL)
			nRes= NO_CHAN;
	}
	if (!nRes)
	{
		*pnChan= ((SBaseOut*)pHeadIn)->sBaseIn.nChan;
		*pllStart= ((SBaseOut*)pHeadIn)->llLargeInteger;
	}
	free(pHeadIn);
	free(pHeadOut);
	CloseHandle(hPipe);
	return nRes;

	/*
	SBase* pBaseS= (SBase*)pInit;
	pBaseS->dwSize= sizeof(SBase)+sizeof(SValveInit);
	pBaseS->eType= eFTDIMultiWriteInit;
	++pBaseS;
	SValveInit* pValve= (SValveInit*)pBaseS;
	pValve->dwBoards= dwBoards;
	pValve->dwClkPerData= dwClkPerData;
	pValve->ucClk= 1;
	pValve->ucData= 3;
	pValve->ucLatch= 2;
	++pValve;
	pBaseS= (SBase*)pValve;
	pBaseS->dwSize= sizeof(SBase)+sizeof(SADCInit);
	pBaseS->eType= eFTDIADCInit;
	++pBaseS;
	SADCInit* pADC= (SADCInit*)pBaseS;
	pADC->bChan2= CHAN2_ACTIVE;
	pADC->dwBuff= dwTransSize;
	pADC->bReverseBytes= true;
	pADC->bStatusReg= true;
	pADC->dwDataPerTrans= ADC_DATA_PER_PACKET;
	pADC->ucBitsPerData= 24;
	pADC->ucClk= 0;
	pADC->ucLowestDataBit= 4;
	dwBytes= 0;
	if (!WriteFile(hPipe, pHead, 2*sizeof(SBaseIn)+sizeof(SChanInitFTDI)+2*sizeof(SBase)+sizeof(SValveInit)+sizeof(SADCInit), &dwBytes, NULL))
	{
		nRes= WIN_ERROR(GetLastError());
		free(pHead);
		CloseHandle(hPipe);
		return nRes;
	}
	free(pHead);

	pHead= malloc(MIN_BUFF_OUT);
	if (!pHead)
		return NO_SYS_RESOURCE;

	if (!ReadFile(hPipe, pHead, MIN_BUFF_OUT, &dwBytes, NULL))
	{
		nRes= WIN_ERROR(GetLastError());
		free(pHead);
		CloseHandle(hPipe);
		return nRes;
	}
	if (dwBytes != sizeof(SBaseOut) || ((SBaseOut*)pHead)->sBaseIn.eType != eResponseEx)	// there's no FTDI channel
	{
		CloseHandle(hPipe);
		free(pHead);
		return UNEXPECTED_READ;
	}
	nRes= ((SBaseOut*)pHead)->sBaseIn.nError;
	*pnChan= ((SBaseOut*)pHead)->sBaseIn.nChan;
	*pllStart= ((SBaseOut*)pHead)->llLargeInteger;

	free(pHead);
	CloseHandle(hPipe);
	return nRes;*/
}

extern "C"	BARSTDLL_API int __stdcall BarstCreateRTVChan(const TCHAR* szPipe, DWORD dwTimeout,	SChanInitRTV* psChanInit, 
	int* pnManChan, int nChan, LARGE_INTEGER *pllStart)
{
	if (!szPipe || !psChanInit || !pnManChan || !pllStart)
		return BAD_INPUT_PARAMS;

	// open ftdi manager
	int nRes= BarstCreateManager(szPipe, dwTimeout, eRTVMan, pnManChan);
	if (nRes==ALREADY_OPEN)
		nRes= 0;
	if (nRes)
		return nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);
	if(nRes)
		return nRes;

	DWORD dwRead= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV);
	void* pHeadOut= malloc(2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitRTV));	// for later
	void* pHeadIn= malloc(sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV));
	if (!pHeadOut || !pHeadIn)
	{
		CloseHandle(hPipe);
		free(pHeadOut);
		free(pHeadIn);
		return NO_SYS_RESOURCE;
	}
	// now we need to open the device
	SBaseIn* pBase= (SBaseIn*)pHeadOut;
	pBase->dwSize= 2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitRTV);
	pBase->eType= ePassOn;
	pBase->nChan= *pnManChan;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitRTV);
	pBase->eType= eSet;
	pBase->nChan= nChan;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SChanInitRTV)+sizeof(SBase);	// SBase*
	pBase->eType= eRTVChanInit;
	memcpy((char*)pBase+sizeof(SBase), psChanInit, sizeof(SChanInitRTV));

	nRes= BarstWriteRead(hPipe, 2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitRTV), 
		pHeadOut, &dwRead, pHeadIn);
	if (!nRes)
	{
		if ((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut) || dwRead == sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV)) && 
			((SBaseIn*)pHeadIn)->dwSize == dwRead && ((SBaseIn*)pHeadIn)->nError)
			nRes= ((SBaseIn*)pHeadIn)->nError;
		else if (!(dwRead == sizeof(SBaseOut)+sizeof(SBase)+sizeof(SChanInitRTV) && ((SBaseIn*)pHeadIn)->dwSize == dwRead && 
			((SBaseIn*)pHeadIn)->eType == eResponseExL && ((SBase*)((char*)pHeadIn+sizeof(SBaseOut)))->eType == eRTVChanInit))
			nRes= NO_CHAN;
	}
	if (!nRes)
	{
		memcpy(psChanInit, (char*)pHeadIn+sizeof(SBaseOut)+sizeof(SBase), sizeof(SChanInitRTV));
		*pllStart= ((SBaseOut*)pHeadIn)->llLargeInteger;
	}
	free(pHeadIn);
	free(pHeadOut);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstCreateSerialChan(const TCHAR* szPipe, DWORD dwTimeout,	const SChanInitSerial* psChanInit, 
	int* pnManChan, int* pnChan, LARGE_INTEGER *pllStart)
{
	if (!szPipe || !psChanInit || !pnManChan  || !pnChan || !pllStart)
		return BAD_INPUT_PARAMS;

	// open ftdi manager
	int nRes= BarstCreateManager(szPipe, dwTimeout, eSerialMan, pnManChan);
	if (nRes==ALREADY_OPEN)
		nRes= 0;
	if (nRes)
		return nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);
	if(nRes)
		return nRes;

	DWORD dwRead= sizeof(SBaseOut);
	void* pHeadOut= malloc(2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitSerial));	// for later
	void* pHeadIn= malloc(sizeof(SBaseOut));
	if (!pHeadOut || !pHeadIn)
	{
		CloseHandle(hPipe);
		free(pHeadOut);
		free(pHeadIn);
		return NO_SYS_RESOURCE;
	}
	// now we need to open the device
	SBaseIn* pBase= (SBaseIn*)pHeadOut;
	pBase->dwSize= 2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitSerial);
	pBase->eType= ePassOn;
	pBase->nChan= *pnManChan;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitSerial);
	pBase->eType= eSet;
	pBase->nChan= -1;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SChanInitSerial)+sizeof(SBase);	// SBase*
	pBase->eType= eSerialChanInit;
	memcpy((char*)pBase+sizeof(SBase), psChanInit, sizeof(SChanInitSerial));

	nRes= BarstWriteRead(hPipe, 2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SChanInitSerial), 
		pHeadOut, &dwRead, pHeadIn);
	if (!nRes)
	{
		if ((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && 
			((SBaseIn*)pHeadIn)->dwSize == dwRead && ((SBaseIn*)pHeadIn)->nError)
			nRes= ((SBaseIn*)pHeadIn)->nError;
		else if (!(dwRead == sizeof(SBaseOut) && ((SBaseIn*)pHeadIn)->dwSize == dwRead && 
			((SBaseIn*)pHeadIn)->eType == eResponseExL))
			nRes= NO_CHAN;
	}
	if (!nRes)
	{
		*pnChan= ((SBaseOut*)pHeadIn)->sBaseIn.nChan;
		*pllStart= ((SBaseOut*)pHeadIn)->llLargeInteger;
	}
	free(pHeadIn);
	free(pHeadOut);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstCloseChan(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan, int nChan)
{
	if (!szPipe)
		return BAD_INPUT_PARAMS;
	int nManChan;	// get manager
	int nRes= BarstCreateManager(szPipe, dwTimeout, eMan, &nManChan);
	if (nRes==ALREADY_OPEN)
		nRes= 0;
	if (nRes)
		return nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);
	if(nRes)
		return nRes;

	void* pHeadOut= malloc(2*sizeof(SBaseIn));
	void* pHeadIn= malloc(sizeof(SBaseIn));
	if (!pHeadOut || !pHeadIn)
	{
		CloseHandle(hPipe);
		free(pHeadOut);
		free(pHeadIn);
		return NO_SYS_RESOURCE;
	}
	SBaseIn* pBase= (SBaseIn*)pHeadOut;	// tell it to pass on and delete
	pBase->dwSize= 2*sizeof(SBaseIn);
	pBase->eType= ePassOn;
	pBase->nChan= nManChan;
	pBase->nError= 0;
	++pBase;
	pBase->dwSize= sizeof(SBaseIn);
	pBase->eType= eDelete;
	pBase->nChan= nChan;
	pBase->nError= 0;
	DWORD dwRead= sizeof(SBaseIn);
	nRes= BarstWriteRead(hPipe, 2*sizeof(SBaseIn), pHeadOut, &dwRead, pHeadIn);
	if (!nRes)
	{
		if (dwRead != sizeof(SBaseIn))
			nRes= UNEXPECTED_READ;
		else
			nRes= ((SBaseIn*)pHeadIn)->nError;
	}

	free(pHeadIn);
	free(pHeadOut);
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstSetDeviceStatus(const TCHAR* szPipe, DWORD dwTimeout, int nChan, bool bActivate)
{
	if (!szPipe)
		return BAD_INPUT_PARAMS;
	SBaseOut sBaseOut;
	int nRes= BarstGetManDeviceInfo(szPipe, dwTimeout, nChan, &sBaseOut);	// find out if device is already in required state
	if (nRes || ((bActivate && sBaseOut.bActive) || (!bActivate && !sBaseOut.bActive)))
		return nRes;
	HANDLE hPipe;
	nRes= BarstOpenPipe(szPipe, GENERIC_WRITE|GENERIC_READ, dwTimeout, &hPipe);
	if (nRes)
		return nRes;

	SBaseIn sBase;
	sBase.dwSize= sizeof(SBaseIn);
	sBase.eType= bActivate?eActivate:eInactivate;
	sBase.nChan= nChan;
	sBase.nError= 0;
	DWORD dwRead= sizeof(SBaseIn);
	SBaseIn sBaseRead;
	nRes= BarstWriteRead(hPipe, sizeof(SBaseIn), &sBase, &dwRead, &sBaseRead);
	if (!nRes)
	{
		if (dwRead != sizeof(SBaseIn) || (sBaseRead.eType != (bActivate?eActivate:eInactivate) && !sBaseRead.nError))
			nRes= UNEXPECTED_READ;
		else
			nRes= sBaseRead.nError;
	}
	CloseHandle(hPipe);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstSetDeviceStatusTrigger(HANDLE hPipe, int nChan, bool bActivate)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE)
		return BAD_INPUT_PARAMS;

	SBaseIn sBase;
	sBase.dwSize= sizeof(SBaseIn);
	sBase.eType= bActivate?eActivate:eInactivate;
	sBase.nChan= nChan;
	sBase.nError= 0;
	DWORD dwRead= sizeof(SBaseIn);
	SBaseIn sBaseRead;
	int nRes= BarstWriteRead(hPipe, sizeof(SBaseIn), &sBase, &dwRead, &sBaseRead);
	if (!nRes)
	{
		if (dwRead != sizeof(SBaseIn) || (sBaseRead.eType != (bActivate?eActivate:eInactivate) && !sBaseRead.nError))
			nRes= UNEXPECTED_READ;
		else
			nRes= sBaseRead.nError;
	}
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstFTDIWriteMultiData(HANDLE hPipe, int nChan, const SValveData* aValveData, DWORD dwCount, 
	double* pdTime)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !aValveData || !dwCount || !pdTime)
		return BAD_INPUT_PARAMS;
	SBaseIn* pBase= (SBaseIn*)malloc(2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SValveData)*dwCount);
	if (!pBase)
		return NO_SYS_RESOURCE;
	pBase->dwSize= 2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SValveData)*dwCount;
	pBase->eType= ePassOn;
	pBase->nChan= nChan;
	pBase->nError= 0;
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->dwSize= sizeof(SBaseIn)+sizeof(SBase)+sizeof(SValveData)*dwCount;
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->eType= eData;
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->nChan= -1;
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->nError= 0;
	((SBase*)((char*)pBase+2*sizeof(SBaseIn)))->dwSize= sizeof(SBase)+sizeof(SValveData)*dwCount;
	((SBase*)((char*)pBase+2*sizeof(SBaseIn)))->eType= eFTDIMultiWriteData;
	memcpy((char*)pBase+sizeof(SBase)+2*sizeof(SBaseIn), aValveData, sizeof(SValveData)*dwCount);

	DWORD dwRead= sizeof(SBaseOut);
	SBaseOut sBaseRead;
	int nRes= BarstWriteRead(hPipe, pBase->dwSize, pBase, &dwRead, &sBaseRead);
	if (!nRes)
	{
		if ((dwRead != sizeof(SBaseOut) && dwRead != sizeof(SBaseIn)) || ((dwRead == sizeof(SBaseIn) || sBaseRead.sBaseIn.eType != eResponseExD) 
			&& !sBaseRead.sBaseIn.nError))
			nRes= UNEXPECTED_READ;
		else
			nRes= sBaseRead.sBaseIn.nError;
	}
	*pdTime= sBaseRead.dDouble;
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstFTDIWritePinData(HANDLE hPipe, int nChan, const SPinWData* asData, const unsigned char* aucBufData, 
	DWORD dwCount, double* pdTime)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !asData || !dwCount || !pdTime)
		return BAD_INPUT_PARAMS;
	const DWORD dwSize= 2*sizeof(SBaseIn)+sizeof(SBase)+(aucBufData?(sizeof(SPinWData)+sizeof(char)*dwCount):(sizeof(SPinWData)*dwCount));

	SBaseIn* pBase= (SBaseIn*)malloc(dwSize);
	if (!pBase)
		return NO_SYS_RESOURCE;
	pBase->dwSize= dwSize;
	pBase->eType= ePassOn;
	pBase->nChan= nChan;
	pBase->nError= 0;
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->dwSize= dwSize-sizeof(SBaseIn);
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->eType= eData;
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->nChan= -1;
	((SBaseIn*)((char*)pBase+sizeof(SBaseIn)))->nError= 0;
	((SBase*)((char*)pBase+2*sizeof(SBaseIn)))->dwSize= dwSize-2*sizeof(SBaseIn);
	((SBase*)((char*)pBase+2*sizeof(SBaseIn)))->eType= aucBufData?eFTDIPinWDataBufArray:eFTDIPinWDataArray;
	if (aucBufData)
	{
		memcpy((char*)pBase+2*sizeof(SBaseIn)+sizeof(SBase), asData, sizeof(SPinWData));
		memcpy((char*)pBase+2*sizeof(SBaseIn)+sizeof(SBase)+sizeof(SPinWData), aucBufData, sizeof(char)*dwCount);
	} else
		memcpy((char*)pBase+2*sizeof(SBaseIn)+sizeof(SBase), asData, sizeof(SPinWData)*dwCount);

	DWORD dwRead= sizeof(SBaseOut);
	SBaseOut sBaseRead;
	int nRes= BarstWriteRead(hPipe, pBase->dwSize, pBase, &dwRead, &sBaseRead);
	if (!nRes)
	{
		if ((dwRead != sizeof(SBaseOut) && dwRead != sizeof(SBaseIn)) || ((dwRead == sizeof(SBaseIn) || sBaseRead.sBaseIn.eType != eResponseExD) 
			&& !sBaseRead.sBaseIn.nError))
			nRes= UNEXPECTED_READ;
		else
			nRes= sBaseRead.sBaseIn.nError;
	}
	*pdTime= sBaseRead.dDouble;
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstSendDeviceTrigger(HANDLE hPipe, int nChan)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE)
		return BAD_INPUT_PARAMS;

	SBaseIn* pBaseWrite= (SBaseIn*)malloc(2*sizeof(SBaseIn));
	DWORD dwRead= 0;
	if (!pBaseWrite)
		return NO_SYS_RESOURCE;
	pBaseWrite->dwSize= 2*sizeof(SBaseIn);
	pBaseWrite->eType= ePassOn;
	pBaseWrite->nChan= nChan;
	pBaseWrite->nError= 0;
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->dwSize= sizeof(SBaseIn);
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->eType= eTrigger;
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->nChan= -1;
	((SBaseIn*)((char*)pBaseWrite+sizeof(SBaseIn)))->nError= 0;
	int nRes= BarstWriteRead(hPipe, pBaseWrite->dwSize, pBaseWrite, &dwRead, NULL);	// request version
	free(pBaseWrite);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstFTDIReadADCData(HANDLE hPipe, SADCData* psADC, DWORD* adwChan1Data, DWORD* adwChan2Data, 
	DWORD dwChanSize)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !psADC || (!adwChan1Data && !adwChan2Data) || !dwChanSize)
		return BAD_INPUT_PARAMS;
	const DWORD dwSize= sizeof(SADCData) + dwChanSize * sizeof(DWORD) * (adwChan1Data && adwChan2Data?2:1);
	int nRes= 0;
	SADCData* psADCInfo= (SADCData*)malloc(dwSize);
	if (!psADCInfo)
		return NO_SYS_RESOURCE;

	DWORD dwRead= dwSize;
	if (!ReadFile(hPipe, psADCInfo, dwSize, &dwRead, NULL))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		free(psADCInfo);
		return nRes;
	}
	if ((dwRead != sizeof(SBaseIn) && dwRead != dwSize) || (dwRead == sizeof(SBaseIn) && !psADCInfo->sDataBase.nError)
		|| (dwRead == dwSize && psADCInfo->sBase.eType != eADCData))
		nRes= UNEXPECTED_READ;
	else if (dwRead == sizeof(SBaseIn))
		nRes= psADCInfo->sDataBase.nError;
	else if (dwSize != dwRead)	// make sure buffer size match, value is always there, even if one chan is inactive
		nRes= SIZE_MISSMATCH;
	if (nRes)
	{
		free(psADCInfo);
		return nRes;
	}
	if ((psADCInfo->dwCount1 && !adwChan1Data) || (psADCInfo->dwCount2 && !adwChan2Data))
	{
		free(psADCInfo);
		return BAD_INPUT_PARAMS;
	}
	
	*psADC= *psADCInfo;
	if (psADCInfo->dwCount1)
		memcpy(adwChan1Data, (char*)psADCInfo+sizeof(SADCData), sizeof(DWORD)*psADCInfo->dwCount1);
	if (psADCInfo->dwCount2)
		memcpy(adwChan2Data, (char*)psADCInfo+sizeof(SADCData)+sizeof(DWORD)*psADCInfo->dwChan2Start, sizeof(DWORD)*psADCInfo->dwCount2);

	free(psADCInfo);
	return 0;
}

extern "C"	BARSTDLL_API int __stdcall BarstFTDIReadMultiData(HANDLE hPipe, bool* abData, DWORD dwCount, double* pdTime)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !abData || !dwCount || !pdTime)
		return BAD_INPUT_PARAMS;
	const DWORD dwSize= sizeof(SBaseOut)+sizeof(SBase)+dwCount*sizeof(bool);
	int nRes= 0;
	SBaseOut* psBase= (SBaseOut*)malloc(dwSize);
	if (!psBase)
		return NO_SYS_RESOURCE;

	DWORD dwRead= dwSize;
	if (!ReadFile(hPipe, psBase, dwSize, &dwRead, NULL))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		free(psBase);
		return nRes;
	}
	if ((dwRead != sizeof(SBaseIn) && dwRead != sizeof(SBaseOut) && dwRead != dwSize) || 
		((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && !psBase->sBaseIn.nError) ||
		(dwRead == dwSize && !psBase->sBaseIn.nError && (psBase->sBaseIn.eType != eResponseExD || 
		((SBase*)((char*)psBase+sizeof(SBaseOut)))->eType != eFTDIMultiReadData)))
		nRes= UNEXPECTED_READ;
	else if (psBase->sBaseIn.nError)
		nRes= psBase->sBaseIn.nError;
	if (nRes)
	{
		free(psBase);
		return nRes;
	}
	*pdTime= psBase->dDouble;
	memcpy(abData, (char*)psBase+sizeof(SBaseOut)+sizeof(SBase), dwCount*sizeof(bool));

	free(psBase);
	return 0;
}

extern "C"	BARSTDLL_API int __stdcall BarstFTDIReadPinData(HANDLE hPipe, unsigned char* aucData, DWORD dwCount, double* pdTime)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !aucData || !dwCount || !pdTime)
		return BAD_INPUT_PARAMS;
	const DWORD dwSize= sizeof(SBaseOut)+sizeof(SBase)+dwCount*sizeof(char);
	int nRes= 0;
	SBaseOut* psBase= (SBaseOut*)malloc(dwSize);
	if (!psBase)
		return NO_SYS_RESOURCE;

	DWORD dwRead= dwSize;
	if (!ReadFile(hPipe, psBase, dwSize, &dwRead, NULL))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		free(psBase);
		return nRes;
	}
	if ((dwRead != sizeof(SBaseIn) && dwRead != sizeof(SBaseOut) && dwRead != dwSize) || 
		((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && !psBase->sBaseIn.nError) ||
		(dwRead == dwSize && !psBase->sBaseIn.nError && (psBase->sBaseIn.eType != eResponseExD || 
		((SBase*)((char*)psBase+sizeof(SBaseOut)))->eType != eFTDIPinRDataArray)))
		nRes= UNEXPECTED_READ;
	else if (psBase->sBaseIn.nError)
		nRes= psBase->sBaseIn.nError;
	if (nRes)
	{
		free(psBase);
		return nRes;
	}
	*pdTime= psBase->dDouble;
	memcpy(aucData, (char*)psBase+sizeof(SBaseOut)+sizeof(SBase), dwCount*sizeof(char));

	free(psBase);
	return 0;
}

extern "C"	BARSTDLL_API int __stdcall BarstRTVReadImageData(HANDLE hPipe, unsigned char* aucData, DWORD dwSize, double* pdTime)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !aucData || !dwSize || !pdTime)
		return BAD_INPUT_PARAMS;
	int nRes= 0;
	SBaseOut* psBase= (SBaseOut*)malloc(dwSize+sizeof(SBaseOut)+sizeof(SBase));
	if (!psBase)
		return NO_SYS_RESOURCE;

	DWORD dwRead= dwSize+sizeof(SBaseOut)+sizeof(SBase);
	if (!ReadFile(hPipe, psBase, dwRead, &dwRead, NULL))
	{
		nRes= WIN_ERROR(GetLastError(), nRes);
		free(psBase);
		return nRes;
	}
	if ((dwRead != sizeof(SBaseIn) && dwRead != sizeof(SBaseOut) && dwRead != dwSize+sizeof(SBaseOut)+sizeof(SBase)) || 
		((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && !psBase->sBaseIn.nError) ||
		(dwRead == dwSize+sizeof(SBaseOut)+sizeof(SBase) && !psBase->sBaseIn.nError && (psBase->sBaseIn.eType != eResponseExD || 
		((SBase*)((char*)psBase+sizeof(SBaseOut)))->eType != eRTVImageBuf)))
		nRes= UNEXPECTED_READ;
	else if (psBase->sBaseIn.nError)
		nRes= psBase->sBaseIn.nError;
	if (nRes)
	{
		free(psBase);
		return nRes;
	}
	*pdTime= psBase->dDouble;
	memcpy(aucData, (char*)psBase+sizeof(SBaseOut)+sizeof(SBase), dwSize*sizeof(char));

	free(psBase);
	return 0;
}

extern "C"	BARSTDLL_API int __stdcall BarstSerialWrite(HANDLE hPipe, int nChan, SSerialData* psData, const char* acData, 
	double* pdTime)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !psData || !acData || !pdTime)
		return BAD_INPUT_PARAMS;
	DWORD dwSize= sizeof(SBaseIn)+sizeof(SBase)+sizeof(SSerialData)+sizeof(char)*psData->dwSize;
	DWORD dwRead= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData);
	SBaseIn* pBase= (SBaseIn*)malloc(dwSize);
	SBaseOut* pBaseOut= (SBaseOut*)malloc(dwRead);
	if (!pBase || !pBaseOut)
	{
		free(pBase);
		free(pBaseOut);
		return NO_SYS_RESOURCE;
	}
	pBase->dwSize= dwSize;
	pBase->eType= eData;
	pBase->nChan= nChan;
	pBase->nError= 0;
	((SBase*)((char*)pBase+sizeof(SBaseIn)))->dwSize= dwSize-sizeof(SBaseIn);
	((SBase*)((char*)pBase+sizeof(SBaseIn)))->eType= eSerialWriteData;
	memcpy((char*)pBase+sizeof(SBaseIn)+sizeof(SBase), psData, sizeof(SSerialData));
	memcpy((char*)pBase+sizeof(SBaseIn)+sizeof(SBase)+sizeof(SSerialData), acData, sizeof(char)*psData->dwSize);

	int nRes= BarstWriteRead(hPipe, dwSize, pBase, &dwRead, pBaseOut);
	if (!nRes)
	{
		if ((dwRead != sizeof(SBaseOut) && dwRead != sizeof(SBaseIn) && dwRead != 
			sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData)) || 
			((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && !pBaseOut->sBaseIn.nError) ||
			(dwRead == sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData) && (pBaseOut->sBaseIn.eType != eResponseExD ||
			((SBase*)((char*)pBaseOut+sizeof(SBaseOut)))->eType != eSerialWriteData)))
			nRes= UNEXPECTED_READ;
		else if (dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut))
			nRes= pBaseOut->sBaseIn.nError;
		else
		{
			nRes= pBaseOut->sBaseIn.nError;
			*pdTime= pBaseOut->dDouble;
			*psData= *((SSerialData*)((char*)pBaseOut+sizeof(SBaseOut)+sizeof(SBase)));
		}
	}

	free(pBase);
	free(pBaseOut);
	return nRes;
}

extern "C"	BARSTDLL_API int __stdcall BarstSerialRead(HANDLE hPipe, int nChan, SSerialData* psData, char* acData, 
	double* pdTime)
{
	if (!hPipe || hPipe == INVALID_HANDLE_VALUE || !psData || !acData || !pdTime)
		return BAD_INPUT_PARAMS;
	DWORD dwSize= sizeof(SBaseIn)+sizeof(SBase)+sizeof(SSerialData);
	DWORD dwRead= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData)+sizeof(char)*psData->dwSize;

	SBaseIn* pBase= (SBaseIn*)malloc(dwSize);
	SBaseOut* pBaseOut= (SBaseOut*)malloc(dwRead);
	if (!pBase || !pBaseOut)
	{
		free(pBase);
		free(pBaseOut);
		return NO_SYS_RESOURCE;
	}
	pBase->dwSize= dwSize;
	pBase->eType= eTrigger;
	pBase->nChan= nChan;
	pBase->nError= 0;
	((SBase*)((char*)pBase+sizeof(SBaseIn)))->dwSize= dwSize-sizeof(SBaseIn);
	((SBase*)((char*)pBase+sizeof(SBaseIn)))->eType= eSerialReadData;
	memcpy((char*)pBase+sizeof(SBaseIn)+sizeof(SBase), psData, sizeof(SSerialData));

	int nRes= BarstWriteRead(hPipe, dwSize, pBase, &dwRead, pBaseOut);
	if (!nRes)
	{
		if ((dwRead != sizeof(SBaseOut) && dwRead != sizeof(SBaseIn) && dwRead <
			sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData)) || 
			((dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut)) && !pBaseOut->sBaseIn.nError) ||
			(dwRead >= sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData) && (pBaseOut->sBaseIn.eType != eResponseExD ||
			((SBase*)((char*)pBaseOut+sizeof(SBaseOut)))->eType != eSerialReadData ||
			((SSerialData*)((char*)pBaseOut+sizeof(SBaseOut)+sizeof(SBase)))->dwSize > psData->dwSize ||
			dwRead != sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData)+sizeof(char)*
			((SSerialData*)((char*)pBaseOut+sizeof(SBaseOut)+sizeof(SBase)))->dwSize)))
			nRes= UNEXPECTED_READ;
		else if (dwRead == sizeof(SBaseIn) || dwRead == sizeof(SBaseOut))
			nRes= pBaseOut->sBaseIn.nError;
		else
		{
			nRes= pBaseOut->sBaseIn.nError;
			*pdTime= pBaseOut->dDouble;
			*psData= *((SSerialData*)((char*)pBaseOut+sizeof(SBaseOut)+sizeof(SBase)));
			memcpy(acData, (char*)pBaseOut+sizeof(SBaseOut)+sizeof(SBase)+sizeof(SSerialData), 
				sizeof(char)*((SSerialData*)((char*)pBaseOut+sizeof(SBaseOut)+sizeof(SBase)))->dwSize);
		}
	}
	return nRes;
}

extern "C"	BARSTDLL_API double __stdcall BarstCurrentTime(LARGE_INTEGER llStart)
{
	return s_cTimer.Seconds(llStart);
}




int FindProcessName(const TCHAR *szProcess, DWORD* dwProcessId)
{
	if (!szProcess || !dwProcessId)
		return BAD_INPUT_PARAMS;
	*dwProcessId= 0;
	int nRes= 0;
	DWORD adwIds[1024];
	HMODULE hMod;

	DWORD dwLen, dwBytes;
	if (!EnumProcesses(adwIds, 1024*sizeof(DWORD), &dwLen))
		return WIN_ERROR(GetLastError(), nRes);
	if (dwLen == 1000)
		return BUFF_TOO_SMALL;
	dwLen= dwLen/sizeof(DWORD);

	HANDLE hProcess;
	for (DWORD i= 0; i<dwLen; ++i)
	{
		TCHAR szName[MAX_PATH]= _T("<unknown>");
		hProcess= OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, adwIds[i]);
		if (hProcess)
			if(EnumProcessModules(hProcess, &hMod, sizeof(HMODULE), &dwBytes))
				GetModuleBaseName(hProcess, hMod, szName, sizeof(szName)/sizeof(TCHAR));
		CloseHandle(hProcess);
		if (!_tcscmp(szName, szProcess))
		{
			*dwProcessId= adwIds[i];
			return 0;
		}
	}
	return NOT_FOUND;
}