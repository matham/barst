/** Defines the named pipe communicator. */

#ifndef _CPL_NAMED_PIPES_H_
#define _CPL_NAMED_PIPES_H_

#include <Windows.h>
#include <string>
#include <list>
#include <vector>
#include "mem pool.h"
#include "base classses.h"
#include "cpl queue.h"
#include <sstream>


extern __int64 g_llMaxQueueBytes;
extern void InitializeQueueLimit();




// we proceed with states downwards
enum ePipeStatus{
	kCreated,		// pipe was just created and isn't yet connected
	kConnecting,	// pipe is connecting or connected
	kReading		// we're reading the pipe.
};

// resource which stores all the pipe's info
typedef struct SPipeResource
{
	HANDLE		hPipe;	// handle to pipe
	HANDLE		hEvent;	// general event, including signals reading
	OVERLAPPED	oOverlap;	// struct used for connecting and reading
	BOOL		fPending;	// if reading or connecting is pending
	ePipeStatus	eStatus;	// pipe's status
	HANDLE		hWriteEvent;	// event used to sginal write finished
	OVERLAPPED	oWriteOverlap;	// struct for async writing
	BOOL		fWritePending;	// if a write is pending
	__int64		llId;			// unique identifier for this pipe
	void*		pRead;	// hodls buffer into which we're currently reading
	CQueue<SData*>	cWriteQueue;	// holds data to be written to pipe

	SPipeResource(HANDLE hWriteEventE) : cWriteQueue(hWriteEventE)
	{
		memset(&hPipe, 0, offsetof(SPipeResource, cWriteQueue));
		eStatus= kCreated;
		hWriteEvent= hWriteEventE;
		oWriteOverlap.hEvent= hWriteEvent;
	}

	~SPipeResource()	// queue must be emptied before calling this, otherwise memory leak
	{
		//FlushFileBuffers(hPipe);
		DisconnectNamedPipe(hPipe);
		CloseHandle(hPipe);
		CloseHandle(hEvent);
		CloseHandle(hWriteEvent);
	}

	void ResetResource()
	{
		memset(&oOverlap, 0, sizeof(OVERLAPPED));
		memset(&oWriteOverlap, 0, sizeof(OVERLAPPED));
		//FlushFileBuffers(hPipe);
		DisconnectNamedPipe(hPipe);
		SetEvent(hEvent);
		ResetEvent(hWriteEvent);
		fWritePending= fPending= FALSE;
		eStatus= kCreated;
		oWriteOverlap.hEvent= hWriteEvent;
		oOverlap.hEvent= hEvent;
	}

} SPipeResource;


/** Implememts named pipes as a comm. Communication is async so that one thread is doing all the work
	async. But, you can have multiple clients connecting simultaneuosly. You call init to start the
	server and close to stop it. Closing stopps all current communications. */
class CPipeServer : public CComm
{
public:
	CPipeServer();
	virtual ~CPipeServer();

	/** szPipe is the pipe to be used. 
		nPipes is the maximum number of pipes instances that can be open at once.
		For instance, if it's 1, only one user can connect at any time. The maximum actually possible instances is in 
		the 30s. If all the pipes are connected to users, another client trying to connect will get a busy error reply.
		dwBuffSizeIn is the max buffer that can be written by the client to the server at once. 
		dwBuffSizeOut is the max buffer that can be read by the client and written by the server at once. 
		cDevice is the device that this pipe will call when a client writes to the pipe.
		cLogBuffer is NULL currently, but would contain a log object.  If g_llMaxQueueBytes is not
		-1, data which would result in larger values than g_llMaxQueueBytes will be simply
		discarded without error.*/
	int Init(const TCHAR szPipe[], int nPipes, DWORD dwBuffSizeIn, DWORD dwBuffSizeOut, CDevice *cDevice, 
		CLogBuffer *cLogBuffer);
	int SendData(const SData *pData, __int64 llId);
	void Close();

	DWORD ThreadProc();	// the thread function that deals with all the clients.
private:
	std::tstring	m_csPipe;	// the pipe name
	int				m_nMaxPipes;	// max number of clients connectable
	int				m_nConnected;	// number of client currently connected.
	DWORD			m_dwBuffSizeIn;
	DWORD			m_dwBuffSizeOut;
	HANDLE			m_hThread;		// thread handle
	HANDLE			m_hDone;		// event set when we want the pipe thread to exit

	std::tostringstream	m_sStream;	// for log

	__int64			m_llNextId;		// the ID of the next client that connects

	std::vector<SPipeResource*>	m_aPipes;	// holds all the pipes
	std::vector<HANDLE>			m_ahEvents;	// holds all the events for the pipes so we can wait on them

	CRITICAL_SECTION			m_sPipeSafe;	// protects access to a pipe
	CRITICAL_SECTION			m_sInitSafe;	// protects access to initialization and exits
	bool						m_bWorking;		// if the instance was initialized already
};


#endif