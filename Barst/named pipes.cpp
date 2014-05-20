

#include "named pipes.h"
#include "Log buffer.h"


/////////////////////////////////////////////////
// Starting point of queue thread
DWORD WINAPI PipeProc(LPVOID lpParameter)
{
	// Call ThreadProc function of pipe object
    return ((CPipeServer*)lpParameter)->ThreadProc();
}



CPipeServer::CPipeServer()
{
	m_hThread= NULL;
	m_hDone= NULL;
	m_pcDevice= NULL;
	m_pcLogBuffer= NULL;
	m_pcMemPool= new CMemPool;
	m_sStream.str(_T(""));
	m_bWorking= false;
	m_llNextId= 0;
	InitializeCriticalSection(&m_sPipeSafe);
	InitializeCriticalSection(&m_sInitSafe);
}

CPipeServer::~CPipeServer()
{
	Close();
	DeleteCriticalSection(&m_sPipeSafe);
	DeleteCriticalSection(&m_sInitSafe);
	delete m_pcMemPool;
}

int CPipeServer::Init(const TCHAR szPipe[], int nPipes, DWORD dwBuffSizeIn, DWORD dwBuffSizeOut, CDevice *cDevice, 
		CLogBuffer *cLogBuffer)
{
	if (m_hThread || !szPipe || nPipes < 1 || !cDevice)	// validate params
		return BAD_INPUT_PARAMS;

	EnterCriticalSection(&m_sInitSafe);
	if (m_bWorking)										// cannot init twice
	{
		LeaveCriticalSection(&m_sInitSafe);
		return ALREADY_OPEN;
	}

	m_csPipe= szPipe;	// save params
	m_pcDevice= cDevice;
	m_pcLogBuffer= cLogBuffer;
	// objects used are: 1 to signal done and then 2 for each pipe handle which limits total number of pipes
	m_nMaxPipes= nPipes>min((MAXIMUM_WAIT_OBJECTS-1)/2, PIPE_UNLIMITED_INSTANCES)?min((MAXIMUM_WAIT_OBJECTS-1)/2, PIPE_UNLIMITED_INSTANCES):nPipes;
	m_dwBuffSizeIn= dwBuffSizeIn < MIN_PIPE_BUF_SIZE ? MIN_PIPE_BUF_SIZE : dwBuffSizeIn;
	m_dwBuffSizeOut= dwBuffSizeOut < MIN_PIPE_BUF_SIZE ? MIN_PIPE_BUF_SIZE : dwBuffSizeOut;
	m_sStream.str(_T(""));
	
	m_hDone= CreateEvent(NULL, TRUE, FALSE, NULL);	// indicates closing
	// first pipe
	SPipeResource *sPipeResource= new SPipeResource(CreateEvent(NULL, TRUE, FALSE, NULL));	// writing event
	sPipeResource->hPipe= CreateNamedPipe(m_csPipe.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 
				m_dwBuffSizeOut, m_dwBuffSizeIn, 0, NULL);
	sPipeResource->hEvent= CreateEvent(NULL, TRUE, FALSE, NULL);	// the first pipe event starts signaled
	sPipeResource->oOverlap.hEvent= sPipeResource->hEvent;
	sPipeResource->pRead= m_pcMemPool->PoolAcquire(m_dwBuffSizeIn);	// read into this memory

	m_aPipes.clear();
	m_aPipes.push_back(sPipeResource);	// first pipe
	m_ahEvents.clear();
	m_ahEvents.push_back(m_hDone);
	m_ahEvents.push_back(sPipeResource->hEvent);		// general pipe event
	m_ahEvents.push_back(sPipeResource->hWriteEvent);	// followed by pipe writing event
	// now verify the resources
	if (!m_hDone || !sPipeResource->hWriteEvent || sPipeResource->hPipe == INVALID_HANDLE_VALUE || !sPipeResource->hEvent
		|| !sPipeResource->pRead)
	{
		Close();
		if (m_pcLogBuffer)
		{
			m_sStream<<_T("3;")<<GetLastError()<<_T(";Couldn't open some pipe resources for the first pipe instance.");
			m_pcLogBuffer->Add(m_sStream.str().c_str());
			m_sStream.str(_T(""));
		}
		LeaveCriticalSection(&m_sInitSafe);
		return NO_SYS_RESOURCE;
	}
	sPipeResource->llId= m_llNextId++;	// assign unique ID for pipe
	sPipeResource->fPending= TRUE;	// assume pending result
	ConnectNamedPipe(sPipeResource->hPipe, &sPipeResource->oOverlap); // connect so pipe is ready before we return
	switch (GetLastError())	
	{
	case ERROR_PIPE_CONNECTED:	// success
		SetEvent(sPipeResource->hEvent);	// set it so we'll read next
		sPipeResource->fPending= FALSE;
	case ERROR_IO_PENDING:
		sPipeResource->eStatus= kConnecting;	// either way we are still connecting
		break;
	default:	// error, just delete this pipe instance
		sPipeResource->llId= m_llNextId++;	// so that we won't get data from previous user
		sPipeResource->ResetResource();		// reset everything
		break;
	}
	m_bWorking= true;

	// now thread that opens connection
	m_hThread= CreateThread(NULL, 0, PipeProc, this, 0, NULL);
	if (!m_hThread)
	{
		Close();
		if (m_pcLogBuffer)
		{
			m_sStream<<_T("3;")<<GetLastError()<<_T(";Couldn't create pipe thread.");
			m_pcLogBuffer->Add(m_sStream.str().c_str());
			m_sStream.str(_T(""));
		}
		LeaveCriticalSection(&m_sInitSafe);
		return NO_SYS_RESOURCE;
	}
	LeaveCriticalSection(&m_sInitSafe);
	return 0;
}

DWORD CPipeServer::ThreadProc()
{
	bool bError= false, bNotEmpty;
	m_nConnected= 0;
	DWORD dwBytes;
	while (1)
	{
		EnterCriticalSection(&m_sPipeSafe);
		// if all the open pipes are connected open another one unless we reached max or error occured before
		if (m_nConnected == m_aPipes.size() && m_aPipes.size() < m_nMaxPipes && !bError)
		{
			SPipeResource *sPipeResource= new SPipeResource(CreateEvent(NULL, TRUE, FALSE, NULL));	// writing event
			sPipeResource->hPipe= CreateNamedPipe(m_csPipe.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 
				m_dwBuffSizeOut, m_dwBuffSizeIn, 0, NULL);
			sPipeResource->hEvent= CreateEvent(NULL, TRUE, TRUE, NULL);	// starts signaled
			sPipeResource->pRead= m_pcMemPool->PoolAcquire(m_dwBuffSizeIn);	// read into this memory
			if (sPipeResource->hPipe == INVALID_HANDLE_VALUE || !sPipeResource->hEvent || !sPipeResource->hWriteEvent
				|| !sPipeResource->pRead)
			{
				if (m_pcLogBuffer)
				{
					m_sStream<<_T("2;")<<GetLastError()<<_T(";Couldn't open some pipe resources for secondary pipe instance.");
					m_pcLogBuffer->Add(m_sStream.str().c_str());
					m_sStream.str(_T(""));
				}
				m_pcMemPool->PoolRelease(sPipeResource->pRead);
				delete sPipeResource;	// automatically closes everything
				bError= true;	// once error occured, don't open new handles
			} else	// save it
			{
				sPipeResource->oOverlap.hEvent= sPipeResource->hEvent;
				sPipeResource->llId= m_llNextId++;
				m_aPipes.push_back(sPipeResource);
				m_ahEvents.push_back(sPipeResource->hEvent);
				m_ahEvents.push_back(sPipeResource->hWriteEvent);
			}
			// now we are ready to connect
		}
		LeaveCriticalSection(&m_sPipeSafe);

		DWORD dwWait= WaitForMultipleObjects((DWORD)m_ahEvents.size(), &m_ahEvents[0], FALSE, INFINITE);
		if (dwWait == WAIT_OBJECT_0)	// first object indicates we need to close.
			break;
		EnterCriticalSection(&m_sPipeSafe);
		if (dwWait-WAIT_OBJECT_0 > m_ahEvents.size()-1 || dwWait-WAIT_OBJECT_0 < 0)
		{
			if (m_pcLogBuffer)
			{
				m_sStream<<_T("1;")<<GetLastError()<<_T(";WaitForMultipleObjects returned invalid index.");
				m_pcLogBuffer->Add(m_sStream.str().c_str());
				m_sStream.str(_T(""));
			}
			LeaveCriticalSection(&m_sPipeSafe);
			continue;
		}

		// now we find the pipe that signaled
		DWORD i= (DWORD)floor((dwWait-WAIT_OBJECT_0-1)/2.0);
		if (!((dwWait-WAIT_OBJECT_0-1)%2))	// first pipe event
		{
			ResetEvent(m_aPipes[i]->hEvent);
			switch (m_aPipes[i]->eStatus)	// what do we need to do on pipe?
			{
			case kCreated:	// we need to connect since it was just created
				m_aPipes[i]->fPending= TRUE;	// assume pending result
				ConnectNamedPipe(m_aPipes[i]->hPipe, &m_aPipes[i]->oOverlap);
				switch (GetLastError())	
				{
				case ERROR_PIPE_CONNECTED:	// success
					SetEvent(m_aPipes[i]->hEvent);	// set it so we'll read next
					m_aPipes[i]->fPending= FALSE;
				case ERROR_IO_PENDING:
					m_aPipes[i]->eStatus= kConnecting;	// either way we are still connecting
					break;
				default:	// error, just delete this pipe instance
					if (m_nMaxPipes == 1)	// don't delete this pipe b/c it will disconnect server
					{
						m_aPipes[i]->llId= m_llNextId++;	// so that we won't get data from previous user
						m_aPipes[i]->ResetResource();		// reset everything
					} else
					{
						delete m_aPipes[i];
						m_aPipes.erase(m_aPipes.begin()+i);
						m_ahEvents.erase(m_ahEvents.begin()+2*i+1, m_ahEvents.begin()+2*i+3);
					}
					break;
				}
				break;
			case kConnecting:	// finish connecting and read
				if (m_aPipes[i]->fPending)	// was pending
				{
					if (!GetOverlappedResult(m_aPipes[i]->hPipe, &m_aPipes[i]->oOverlap, &dwBytes, FALSE))	// failed
					{
						if (m_nMaxPipes == 1)	// don't delete this pipe b/c it will disconnect server
						{
							m_aPipes[i]->llId= m_llNextId++;	// so that we won't get data from previous user
							m_aPipes[i]->ResetResource();		// reset everything
						} else
						{
							delete m_aPipes[i];	// delete this pipe
							m_aPipes.erase(m_aPipes.begin()+i);
							m_ahEvents.erase(m_ahEvents.begin()+2*i+1, m_ahEvents.begin()+2*i+3);
						}
						LeaveCriticalSection(&m_sPipeSafe);
						continue;
					}
					m_aPipes[i]->fPending= FALSE;
				}
				// now we need to read, set so we'll go into reading mode
				++m_nConnected;
				m_aPipes[i]->eStatus= kReading;
				SetEvent(m_aPipes[i]->hEvent);
				break;
			case kReading:
				if (m_aPipes[i]->fPending)	// was pending
				{
					if (!GetOverlappedResult(m_aPipes[i]->hPipe, &m_aPipes[i]->oOverlap, &dwBytes, FALSE))	// failed
					{
						if (m_nMaxPipes == 1)	// don't delete this pipe b/c it will disconnect server
						{
							m_aPipes[i]->llId= m_llNextId++;	// so that we won't get data from previous user
							SData* sData;	// clear write queue
							bool bRes;
							while (m_aPipes[i]->cWriteQueue.GetSize())
							{
								sData= m_aPipes[i]->cWriteQueue.Front(true, bRes);
								if (sData)
									sData->pDevice->Result(sData->pHead, false);
								delete sData;
							}
							m_aPipes[i]->ResetResource();		// reset everything
						} else
						{
							m_pcMemPool->PoolRelease(m_aPipes[i]->pRead);
							delete m_aPipes[i];	// delete this pipe
							m_aPipes.erase(m_aPipes.begin()+i);
							m_ahEvents.erase(m_ahEvents.begin()+2*i+1, m_ahEvents.begin()+2*i+3);
							--m_nConnected;
						}
						LeaveCriticalSection(&m_sPipeSafe);
						continue;
					}
					LeaveCriticalSection(&m_sPipeSafe);
					m_pcDevice->ProcessData(m_aPipes[i]->pRead, dwBytes, m_aPipes[i]->llId);	// finish up
					EnterCriticalSection(&m_sPipeSafe);
					m_aPipes[i]->fPending= FALSE;
				}

				if (ReadFile(m_aPipes[i]->hPipe, m_aPipes[i]->pRead, m_dwBuffSizeIn, &dwBytes, &m_aPipes[i]->oOverlap))
				{
					LeaveCriticalSection(&m_sPipeSafe);
					m_pcDevice->ProcessData(m_aPipes[i]->pRead, dwBytes, m_aPipes[i]->llId);
					EnterCriticalSection(&m_sPipeSafe);
					SetEvent(m_aPipes[i]->hEvent);	// read again
				} else if (GetLastError() == ERROR_IO_PENDING)
				{
					m_aPipes[i]->fPending= TRUE;	// finish later
				} else
				{
					if (m_nMaxPipes == 1)	// don't delete this pipe b/c it will disconnect server
					{
						m_aPipes[i]->llId= m_llNextId++;	// so that we won't get data from previous user
						SData* sData;	// clear write queue
						bool bRes;
						while (m_aPipes[i]->cWriteQueue.GetSize())
						{
							sData= m_aPipes[i]->cWriteQueue.Front(true, bRes);
							if (sData)
								sData->pDevice->Result(sData->pHead, false);
							delete sData;
						}
						m_aPipes[i]->ResetResource();		// reset everything
					} else
					{
						m_pcMemPool->PoolRelease(m_aPipes[i]->pRead);
						delete m_aPipes[i];	// delete this pipe
						m_aPipes.erase(m_aPipes.begin()+i);
						m_ahEvents.erase(m_ahEvents.begin()+2*i+1, m_ahEvents.begin()+2*i+3);
						--m_nConnected;
					}
				}
				break;
			}
		} else
		{
			ResetEvent(m_aPipes[i]->hWriteEvent);
			SData* sData= m_aPipes[i]->cWriteQueue.Front(false, bNotEmpty);	// next item to write/written
			if (!bNotEmpty)	// nothing to write/written
			{
				m_aPipes[i]->fWritePending= FALSE;
				LeaveCriticalSection(&m_sPipeSafe);
				continue;
			}
			if (m_aPipes[i]->fWritePending)	// finish previous write
			{
				sData->pDevice->Result(sData->pHead, GetOverlappedResult(m_aPipes[i]->hPipe, &m_aPipes[i]->oWriteOverlap, &dwBytes, FALSE) 
					&& dwBytes == sData->dwSize);
				m_aPipes[i]->fWritePending= FALSE;
				delete m_aPipes[i]->cWriteQueue.Front(true, bNotEmpty);
				if (m_aPipes[i]->cWriteQueue.GetSize())	// write next element
					SetEvent(m_aPipes[i]->hWriteEvent);
			} else	// write next item
			{
				if (WriteFile(m_aPipes[i]->hPipe, sData->pHead, sData->dwSize, &dwBytes, &m_aPipes[i]->oWriteOverlap))
				{	// success
					sData->pDevice->Result(sData->pHead, true);
					delete m_aPipes[i]->cWriteQueue.Front(true, bNotEmpty);
					if (m_aPipes[i]->cWriteQueue.GetSize())	// write next element
						SetEvent(m_aPipes[i]->hWriteEvent);
				} else if (GetLastError() == ERROR_IO_PENDING)
					m_aPipes[i]->fWritePending= TRUE;
				else
				{
					sData->pDevice->Result(sData->pHead, false);
					delete m_aPipes[i]->cWriteQueue.Front(true, bNotEmpty);
					if (m_aPipes[i]->cWriteQueue.GetSize())	// write next element
						SetEvent(m_aPipes[i]->hWriteEvent);
				}
			}
		}
		LeaveCriticalSection(&m_sPipeSafe);
	}

	return 0;
}

void CPipeServer::Close()
{
	bool bNotEmpty;
	// close thread handle
	EnterCriticalSection(&m_sInitSafe);
	m_bWorking= false;

	DWORD dwRes= WAIT_OBJECT_0 +1;
	if (m_hThread)
	{
		if (m_hDone)
			dwRes= SignalObjectAndWait(m_hDone, m_hThread, 2000, FALSE);
		if (dwRes != WAIT_OBJECT_0)
		{
			TerminateThread(m_hThread, 0);
			DeleteCriticalSection(&m_sPipeSafe);	// we do this so that we can enter cc after close, in case it was terminated in cc.
			InitializeCriticalSection(&m_sPipeSafe);
		}
	}
	for (size_t i= 0; i<m_aPipes.size();++i)
	{
		void* pHead= m_aPipes[i]->pRead;
		while (m_aPipes[i]->cWriteQueue.GetSize())
		{
			SData* pData= m_aPipes[i]->cWriteQueue.Front(true, bNotEmpty);
			if (pData)
			{
				pData->pDevice->Result(pData->pHead, false);
				delete pData;
			}
		}
		delete m_aPipes[i];
		m_pcMemPool->PoolRelease(pHead);
	}
	m_aPipes.clear();
	if (m_hDone) CloseHandle(m_hDone);
	if (m_hThread) CloseHandle(m_hThread);
	m_hThread= NULL;
	m_hDone= NULL;
	m_pcDevice= NULL;
	m_pcLogBuffer= NULL;
	m_sStream.str(_T(""));
	LeaveCriticalSection(&m_sInitSafe);
}

int CPipeServer::SendData(const SData *pData, __int64 llId)
{
	if (!pData || !pData->pHead || !pData->pDevice || !pData->dwSize)
		return TRUE;
	EnterCriticalSection(&m_sInitSafe);	// so that close (terminate) cannot be called on this thread
	if (!m_bWorking)	// haven't activated this comm
	{
		LeaveCriticalSection(&m_sInitSafe);
		return TRUE;
	}

	BOOL fRes= TRUE;	// assume failure
	EnterCriticalSection(&m_sPipeSafe);	// so that pipe won't close on us suddenly
	for (DWORD i= 0; i<m_aPipes.size(); ++i)
	{
		if (m_aPipes[i]->llId == llId)
		{
			m_aPipes[i]->cWriteQueue.Push(new SData(*pData));
			fRes= FALSE;
			break;
		}
	}
	LeaveCriticalSection(&m_sPipeSafe);
	LeaveCriticalSection(&m_sInitSafe);
	return fRes;
}