/** Defines a thread safe queue. */

#ifndef	_CPL_QUEUE_H_
#define _CPL_QUEUE_H_

#include <Windows.h>
#include <queue>
#include <type_traits>


/** Defines a thread safe general FIFO queue. When an object is added to the queue, 
	we only set the event if the queue was empty, if the queue had an element we don't 
	set the event. Also, we never reset the event; it's the users responsibility to 
	reset the event when finished proccesing an element and the queue is empty (or 
	whatever scheme the user wants). */
template <class T>
class CQueue
{
public:
	/** Creates the queue. You pass in a valid event which is set when the queue is empty
		and you enque an element. */
	CQueue(HANDLE hEvent) {m_hEvent= hEvent; InitializeCriticalSection(&m_rLock);}
	virtual ~CQueue() {DeleteCriticalSection(&m_rLock);}

	/** Adds an element to the back of the queue and sets the event (if the queue was empty). */
	void Push(T pHead){
		EnterCriticalSection(&m_rLock);
		if (m_hEvent && !m_cPacketQueue.size())
			SetEvent(m_hEvent);
		m_cPacketQueue.push(pHead);
		LeaveCriticalSection(&m_rLock);
	}

	/** Removes or previews an elemnt from the queue. If bPop is true we return and remove the front
		element from the queue, if false, we only return it but do not remove it from the queue. 
		bValid returns true if the object returned is valid and false otherwise. The returned object 
		can be invalid if the queue was empty. */
	T Front(bool bPop, bool &bValid){
		T pHead= T();
		bValid= false;
		EnterCriticalSection(&m_rLock);
		if (m_cPacketQueue.size() != 0){
			pHead= m_cPacketQueue.front();
			if (bPop)
				m_cPacketQueue.pop();
			bValid= true;
		}
		LeaveCriticalSection(&m_rLock);
		return pHead;
	}
	/**	Returns the current # of items in queue.*/
	int GetSize(){
		EnterCriticalSection(&m_rLock);
		int nSize= (int)m_cPacketQueue.size();
		LeaveCriticalSection(&m_rLock);
		return nSize;
	}
	/**	Resets queue event safely only if the queue is empty. */
	void ResetIfEmpty(){
		EnterCriticalSection(&m_rLock);
		if (!m_cPacketQueue.size())
			ResetEvent(m_hEvent);
		LeaveCriticalSection(&m_rLock);
	}
private:
	std::queue<T>		m_cPacketQueue;	// Queue holding the objects
	CRITICAL_SECTION	m_rLock;		// Protects reading/writing to queue
	HANDLE				m_hEvent;	// Signals when added to queue
};

#endif