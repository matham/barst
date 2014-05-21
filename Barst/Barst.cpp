// Barst.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "cpl defs.h"
#include "named pipes.h"
#include "Log buffer.h"
#include "mem pool.h"
#include "ftdi device.h"
#include "rtv device.h"
#include "serial device.h"
#include "mcdaq_device.h"
#include "misc tools.h"



CMainManager::CMainManager() : CDevice(_T("Main")) 
{
	m_pcComm= NULL;
	m_pcMemPool= new CMemPool;		// main program buffer pool
	m_hClose= CreateEvent(NULL, TRUE, FALSE, NULL);
	m_bError= false;
	if (!m_hClose)
		m_bError= true;
}

// when shutting down, first the pipe is disconnected.
CMainManager::~CMainManager()
{
	if (m_pcComm)
		m_pcComm->Close();
	for (size_t i= 0; i<m_acManagers.size(); ++i)
		delete m_acManagers[i];
	CloseHandle(m_hClose);
	delete m_pcComm;
	delete m_pcMemPool;
}

// only one thread ever calls this since there's only thread in this communicator
void CMainManager::ProcessData(const void *pHead, DWORD dwSize, __int64 llId)
{
	if (m_bError)
		return;
	SData sData;
	sData.pDevice= this;
	sData.dwSize= sizeof(SBaseIn);
	SBaseIn sBase;
	memset(&sBase, 0, sizeof(SBaseIn));
	sBase.dwSize= sizeof(SBaseIn);
	sBase.nChan= -1;
	bool bRes= true;

	if (!pHead || dwSize < sizeof(SBaseIn) || dwSize != ((SBaseIn*)pHead)->dwSize)	// incorrect size read
	{
		sBase.nError= SIZE_MISSMATCH;
	} else if (((SBaseIn*)pHead)->eType == eVersion && ((SBaseIn*)pHead)->dwSize == sizeof(SBaseIn) &&
		((SBaseIn*)pHead)->nChan == -1)	// send lib version
	{
		sBase.dwInfo= BARST_VERSION;
		sBase.eType= eVersion;
	} else if (((SBaseIn*)pHead)->eType == eDelete && ((SBaseIn*)pHead)->dwSize == sizeof(SBaseIn))	// close manager
	{
		if (((SBaseIn*)pHead)->nChan == -1)	// close main program
			SetEvent(m_hClose);
		else if (((SBaseIn*)pHead)->nChan >= 0 && ((SBaseIn*)pHead)->nChan < m_acManagers.size() &&
			m_acManagers[((SBaseIn*)pHead)->nChan])	// close other manager
		{
			delete m_acManagers[((SBaseIn*)pHead)->nChan];
			m_acManagers[((SBaseIn*)pHead)->nChan]= NULL;
		}
		else
			sBase.nError= INVALID_CHANN;
		sBase.nChan= ((SBaseIn*)pHead)->nChan;
		sBase.eType= eDelete;
	} else if (((SBaseIn*)pHead)->eType == eSet && ((SBaseIn*)pHead)->dwSize == sizeof(SBaseIn))	// add a manager
	{
		// prepare array element for manager
		size_t t= 0;
		for (; t < m_acManagers.size() && m_acManagers[t]; ++t);
		if (t == m_acManagers.size())
			m_acManagers.push_back(NULL);
		sBase.eType= eSet;
		switch (((SBaseIn*)pHead)->eType2)	// the device to open
		{
		case eFTDIMan:
		{
			int nPos= -1;
			for (size_t i= 0; i < m_acManagers.size() && nPos == -1; ++i) // make sure it isn't open already
				if (m_acManagers[i] && _tcscmp(FTDI_MAN_STR, m_acManagers[i]->m_csName.c_str()) == 0)
					nPos= (int)i;
			if (nPos != -1)	// found
			{
				sBase.nError= ALREADY_OPEN;
				sBase.nChan= nPos;
				break;
			}
			CManagerFTDI* pMan= new CManagerFTDI(m_pcComm, m_csPipe.c_str(), (int)t, sBase.nError);
			if (!sBase.nError)
			{
				sBase.nChan= (int)t;
				m_acManagers[t]= pMan;
			} else
				delete pMan;
			break;
		}
		case eRTVMan:
		{
			int nPos= -1;
			for (size_t i= 0; i < m_acManagers.size() && nPos == -1; ++i) // make sure it isn't open already
				if (m_acManagers[i] && _tcscmp(RTV_MAN_STR, m_acManagers[i]->m_csName.c_str()) == 0)
					nPos= (int)i;
			if (nPos != -1)	// found
			{
				sBase.nError= ALREADY_OPEN;
				sBase.nChan= nPos;
				break;
			}
			CManagerRTV* pMan= new CManagerRTV(m_pcComm, m_csPipe.c_str(), (int)t, sBase.nError);
			if (!sBase.nError)
			{
				sBase.nChan= (int)t;
				m_acManagers[t]= pMan;
			} else
				delete pMan;
			break;
		}
		case eSerialMan:
		{
			int nPos= -1;
			for (size_t i= 0; i < m_acManagers.size() && nPos == -1; ++i) // make sure it isn't open already
				if (m_acManagers[i] && _tcscmp(SERIAL_MAN_STR, m_acManagers[i]->m_csName.c_str()) == 0)
					nPos= (int)i;
			if (nPos != -1)	// found
			{
				sBase.nError= ALREADY_OPEN;
				sBase.nChan= nPos;
				break;
			}
			CManagerSerial* pMan= new CManagerSerial(m_pcComm, m_csPipe.c_str(), (int)t, sBase.nError);
			if (!sBase.nError)
			{
				sBase.nChan= (int)t;
				m_acManagers[t]= pMan;
			} else
				delete pMan;
			break;
		}
		case eMCDAQMan:
		{
			int nPos= -1;
			for (size_t i= 0; i < m_acManagers.size() && nPos == -1; ++i) // make sure it isn't open already
				if (m_acManagers[i] && _tcscmp(MCDAQ_MAN_STR, m_acManagers[i]->m_csName.c_str()) == 0)
					nPos= (int)i;
			if (nPos != -1)	// found
			{
				sBase.nError= ALREADY_OPEN;
				sBase.nChan= nPos;
				break;
			}
			CManagerMCDAQ* pMan= new CManagerMCDAQ(m_pcComm, m_csPipe.c_str(), (int)t, sBase.nError);
			if (!sBase.nError)
			{
				sBase.nChan= (int)t;
				m_acManagers[t]= pMan;
			} else
				delete pMan;
			break;
		}
		default:
			sBase.nError= INVALID_MAN;
			break;
		}
	} else if (((SBaseIn*)pHead)->nChan == -1 && ((SBaseIn*)pHead)->eType == eQuery)
	{
		sData.dwSize = sizeof(SBaseIn) + sizeof(SBase) + sizeof(SPerfTime);
		void *pHead = sData.pHead = m_pcMemPool->PoolAcquire(sData.dwSize);
		if (pHead)
		{
			((SBaseIn*)pHead)->dwSize= sizeof(SBaseIn) + sizeof(SBase) + sizeof(SPerfTime);
			((SBaseIn*)pHead)->eType= eQuery;
			((SBaseIn*)pHead)->nChan= -1;
			((SBaseIn*)pHead)->nError= 0;
			pHead = (char*)pHead + sizeof(SBaseIn);

			((SBase *)pHead)->dwSize = sizeof(SPerfTime) + sizeof(SBase);
			((SBase *)pHead)->eType = eServerTime;
			pHead= (char *)pHead + sizeof(SBase);

			FILETIME sTime;
			ULARGE_INTEGER ulTime;
			((SPerfTime *)pHead)->dRelativeTime = g_cTimer.Seconds();
			GetSystemTimeAsFileTime(&sTime);
			ulTime.HighPart = sTime.dwHighDateTime;
			ulTime.LowPart = sTime.dwLowDateTime;
			((SPerfTime *)pHead)->dUTCTime = ulTime.QuadPart / 10000000.0;
			m_pcComm->SendData(&sData, llId);
		}
	} else if (((SBaseIn*)pHead)->nChan < 0 || ((SBaseIn*)pHead)->nChan >= m_acManagers.size() ||
		!m_acManagers[((SBaseIn*)pHead)->nChan])	// verify manager exists
	{
		sBase.nError= INVALID_CHANN;
		sBase.eType= ((SBaseIn*)pHead)->eType;
	} else if (((SBaseIn*)pHead)->eType == eQuery && ((SBaseIn*)pHead)->dwSize == sizeof(SBaseIn))	// send info on this manager
	{
		bRes= false;
		DWORD dwResSize= m_acManagers[((SBaseIn*)pHead)->nChan]->GetInfo(NULL, 0);	// size of response
		sData.pHead= m_pcMemPool->PoolAcquire(dwResSize);
		if (sData.pHead)
		{
			m_acManagers[((SBaseIn*)pHead)->nChan]->GetInfo(sData.pHead, dwResSize);	// get response
			sData.dwSize= dwResSize;
			m_pcComm->SendData(&sData, llId);
		}
	} else if (((SBaseIn*)pHead)->eType == ePassOn)	// pass following data to manager
	{
		bRes= false;
		m_acManagers[((SBaseIn*)pHead)->nChan]->ProcessData((char*)pHead + sizeof(SBaseIn), dwSize-sizeof(SBaseIn), llId);
	} else
		sBase.nError= INVALID_COMMAND;

	if (bRes)	// respond
	{
		sData.pHead= m_pcMemPool->PoolAcquire(sData.dwSize);
		if (sData.pHead)
		{
			memcpy(sData.pHead, &sBase, sizeof(SBaseIn));
			m_pcComm->SendData(&sData, llId);
		}
	}
}

int CMainManager::Run(int argc, TCHAR* argv[])
{
	if (argc != 4)
		return BAD_INPUT_PARAMS;
	DWORD dwBuffSizeIn, dwBuffSizeOut;	// size of pipe buffers
	std::tstringstream in(argv[2]), out(argv[3]);
	in >> dwBuffSizeIn;		// user writing to pipe size
	out >> dwBuffSizeOut;	// user reading from pipe
	m_csPipe= argv[1];		// pipe name to use

	if (in.fail() || out.fail() || _tcschr(argv[2],  _T('-')) 
		|| _tcschr(argv[3],  _T('-')))	// cannot be negative #
		return BAD_INPUT_PARAMS;
	// check if it exists already
	HANDLE hPipe= CreateFile(argv[1], GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hPipe != INVALID_HANDLE_VALUE || GetLastError() == ERROR_PIPE_BUSY)
		return ALREADY_OPEN;
	CloseHandle(hPipe);
	m_pcComm= new CPipeServer();	// main program pipe
	int nRes;
	// only use one thread to ensure thread safety
	if (nRes= static_cast<CPipeServer*>(m_pcComm)->Init(argv[1], 1, dwBuffSizeIn, dwBuffSizeOut, this, NULL))
		return nRes;
	if (WAIT_OBJECT_0 != WaitForSingleObject(m_hClose, INFINITE))	// wait here until closing
		return WIN_ERROR(GetLastError(), nRes);
	return 0;
}


int _tmain(int argc, _TCHAR* argv[])
{
	g_cTimer.ResetTimer();
	ShowWindow(GetConsoleWindow(), SW_HIDE);
	CMainManager* pMainManager= new CMainManager;
	int nRes= pMainManager->Run(argc, argv);	// program sits here until stopped
	delete pMainManager;
	return nRes;
}

// WM_COPYDATA http://www.cplusplus.com/forum/windows/23232/
// http://msdn.microsoft.com/en-us/library/windows/desktop/ms633573%28v=vs.85%29.aspx
// http://msdn.microsoft.com/en-us/library/windows/desktop/ms632593%28v=vs.85%29.aspx
