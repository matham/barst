/** Defines parent classes for most object used in the project. */


#ifndef _BASE_CLASSES_H_
#define _BASE_CLASSES_H_

#include "cpl defs.h"
#include <vector>
#include "mem pool.h"


class CDevice;
class CLogBuffer;
class CMemPool;

/*	Struct with which devices send to data to be written to user (through a comm).
	Data is enqueued using this struct.
	*/
typedef struct SData
{
	void*	pHead;	// data to be sent
	DWORD	dwSize;	// data size in bytes
	CDevice* pDevice;	// pointer to device that will be called after data has been sent (Result(...))
} SData;

/** This defines a general communicator which lets you send and recieve massages from user.
	Typically each device gets its own comm for private use. The rule is that every device which has 
	access to a comm and might have enqueued data to the comm to be sent to the user cannot be deleted 
	while that comm is not stopped. I.e. you have to stop the comm and only then can you delete 
	any of the devices which might have had access to the comm. This ensures a comm doesn't try to call
	a deleted device to which it had a pointer but is now an invalid pointer. So you typically: call close 
	on the comm, delete the device and delete the comm. 
	
	At the moment, the rule is that when a device writes something to the comm using SendData, the SData
	struct used will be replicated in the comm (i.e. it doesn't have to be valid after the SendData call 
	is completed), while the device and pHead in SData have to be valid until the comm is done with the
	data. The comm is done with the data after Close is called on the comm or if the comm calls Result(pHead, ...)
	on the device. Then you can delete pHead.

	In addition to accepting data to be sent to a user (in which case using the SData struct we include a pointer
	to a device so that the comm knows who to notify after the data was sent, or not sent) the comm also needs
	to be initialized with a device pointer which it will call when a user has written data to it. In this case,
	the rule is that the data that the comm gives to the device using the ProcessData() function is only valid during 
	the call to the device, so if the device needs access to the data afterwards it should be copied. 
	
	A single comm can communicate with multiple users simultaneuosly. This allows multiple users to send data 
	at the same time. Therefore, we need a way to identify which user sent data to a particular device so when 
	we get a ProcessData() call the device knows who to respond to. This is achieved using the llId parameter
	in the ProcessData() and SendData() function. When a user sends data to a device, the device is notified with
	ProcessData() which includes the llId param to uniquefy the user that sent it. When the device responds with
	SendData() the device includes this llId value so that the comm can reply to the correct user. The llId is
	unique and is incremented with each user that connects until it overflows back to zero (it's a 64-bit int). 
	This guerantees that an llId of -1 is practically never reached, so -1 can used to initialize as an invalid value.*/
class CComm
{
public:
	CComm(){};
	virtual ~CComm() {};
	/** Sends data to a user identified by llId. Function returns TRUE if it failed and FALSE otherwise. 
		At some point it could be extended to return an error code if needed. */
	virtual int	SendData(const SData *pData, __int64 llId) =0;
	/** Stops all communication with this comm, terminates all threads and returns it to its original state. 
		Any data waiting to be sent is not sent but Result() is called on that data. */
	virtual void Close() =0;
protected:
	CDevice*	m_pcDevice;	// the device which gets notified with ProcessData() of user requests.
	CLogBuffer* m_pcLogBuffer;	// could hold a log
	CMemPool*	m_pcMemPool;	// a memory pool used by this comm.
};

/**	This defines a general device. Each channel type object is inherited from this device. A device
	can recieve notifications from a user through a comm through the ProcessData() function. 
	When a device writes to a user through a comm using the SendData() function, the comm notifies 
	the user of success or failure using the Result() function. 
	
	In additon, each type of device has a unique name (defined at compile time) that identifies it. */
class CDevice
{
public:
	/** szName is the device's unique name. */
	CDevice(const TCHAR szName[]) : m_csName(szName){};
	virtual ~CDevice() {};
	/** Comm calls this function when a user sent data to the device. */
	virtual void ProcessData(const void *pHead, DWORD dwSize, __int64 llId) =0;
	/**	Comm calls this function when it finished writing data to user sent by this device. 
		phead is the pHead parameter in the SData struct that was sent with SendData() bPass is
		true if successfull and false otherwise. */
	virtual void Result(void *pHead, bool bPass) =0;
	/** This function copies chennel specific info into pHead which could then be sent to the user in
		response to a query request. If phead is NULL, the function returns the required size 
		of pHead. If non NULL, dwSize if the sise of pHead and the function returns the total size
		of the data copied into pHead. This funcion followes the rules where every sub-struct is proceeded by
		an SBase. Typically, it returns an SBaseOut sturct which holds the name of the device followed
		by channels specific structs. */
	virtual DWORD GetInfo(void* pHead, DWORD dwSize)= 0;

	/** The unique name that identifies this device. */
	const std::tstring m_csName;
protected:
	CComm*		m_pcComm;	// holds a communicator used by device
	CLogBuffer* m_pcLogBuffer;	// a log to which we can write
	CMemPool*	m_pcMemPool;	// pooled memory used instead of malloc()
	// if something went wrong and isn't fixable this is set to true so that all folowing calls to the object
	// return immidiatly. Typically only the constuctor sets it to to true if initialization was bad so that
	// all subsequent calls to the object don't do anything. But if the constructor returns an error (using a 
	// parameter you shouldn't call this object anyway).
	bool		m_bError;
};


/** This is the main program manager. When the program starts this is created once and subsequent calls
	to managers go through this manager. Every call to create a new manager (e.g. FTDI, RTV...) or to create
	a new channel in a manager go through this manager. This manager controlls access to the pipe that is 
	created when the the program is launched with the name passed into the exe. The communicator held by 
	this manager has only one thread ever and only one user can connect to the manager at any time. This ensures
	that multiple users cannot create or delete managers/channels simultaneuosly. So if someone is connected
	to the manager already, another user has to wait until this user is done. See the API for how data is passed
	between managers. */
class CMainManager : public CDevice
{
public:
	CMainManager();
	virtual ~CMainManager();
	/** This function is called by the main exe method and this function waits in here until the exe
		exits. argc and argv are the parameters passed into the main method. See the API for how they
		are parsed. */
	int Run(int argc, TCHAR* argv[]);
	void ProcessData(const void *pHead, DWORD dwSize, __int64 llId);
	void Result(void *pHead, bool bPass) {if (m_pcMemPool) m_pcMemPool->PoolRelease(pHead);}
	DWORD GetInfo(void* pHead, DWORD dwSize){return 0;}
private:
	std::vector<CDevice*>		m_acManagers;	// holds all the managers, their channels numbers are position in this array.
	HANDLE						m_hClose;		// this event is set when we want to the exe to exit
	std::tstring				m_csPipe;		// the main pipe name.
};


/** This defines a manager. The main manager creates these managers and directs communication from
	users to one of these managers which deals with the requests such as queries or creating channels.
	Communication with these managers happen only though the main manager. However, managers
	create channels with their own pipes so communication with a channel doesn't have to go 
	through a manager but happens directly with the channel. Nonetheless, creation and deletion
	of channels do go through the managers. The pipe name of each channels in a manager
	contains both the manager (channel) number and channel number (see API for comm names) 
	details. */
class CManager : public CDevice
{
public:
	/** Create the manager. szName is the unique name of this manager. csPipeName is the name of
		the main manager pipe. nChan is the channel number of this manager in the main manager's 
		channel list. */
	CManager(const TCHAR szName[], const std::tstring csPipeName, int nChan) : CDevice(szName), 
	m_csPipeName(csPipeName), m_nChan(nChan){};
	virtual ~CManager(){};
	virtual void ProcessData(const void *pHead, DWORD dwSize, __int64 llId)= 0;
	virtual void Result(void *pHead, bool bPass)= 0;
	virtual DWORD GetInfo(void* pHead, DWORD dwSize)= 0;

	const std::tstring				m_csPipeName;
	const int						m_nChan;
protected:
	// typically, a manager will load some dll, this dll is held here and is unloaded when the manager is closed.
	HINSTANCE m_hLib;
};

#endif