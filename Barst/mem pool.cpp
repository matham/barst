#include "mem pool.h"



//CMemPool::CMemPool()
//{
//	m_aMemory.clear();
//	InitializeCriticalSection(&m_rPoolSafe);
//}
//
//CMemPool::~CMemPool()
//{
//	while (m_aMemory.size())
//	{
//		free(m_aMemory.back().pMemory);
//		m_aMemory.pop_back();
//	}
//	DeleteCriticalSection(&m_rPoolSafe);
//}
//
//void* CMemPool::PoolAcquire(unsigned long long ullSize)
//{
//	void* pMem= NULL;
//	EnterCriticalSection(&m_rPoolSafe);
//	for (int i= 0; i< m_aMemory.size();++i)	// Find if we have that memory in pool
//	{
//		if (m_aMemory[i].ullSize == ullSize && !m_aMemory[i].bInUse)
//		{
//			pMem= m_aMemory[i].pMemory;
//			m_aMemory[i].bInUse= true;
//			break;
//		}
//	}
//
//	if (!pMem)	// If didn't find, create it
//	{
//		SMemoryItem sMemoryItem;
//		sMemoryItem.bInUse= true;
//		sMemoryItem.ullSize= ullSize;
//		sMemoryItem.pMemory= malloc(ullSize);
//		if (sMemoryItem.pMemory)	// Success
//		{
//			pMem= sMemoryItem.pMemory;
//			m_aMemory.push_back(sMemoryItem);
//		}
//	}
//	LeaveCriticalSection(&m_rPoolSafe);
//	return pMem;
//}
//
//void CMemPool::PoolRelease(const void* pHead)
//{
//	if (!pHead)
//		return;
//	EnterCriticalSection(&m_rPoolSafe);
//	for (int i= 0; i<m_aMemory.size(); ++i)
//	{
//		if (m_aMemory[i].pMemory== pHead)
//		{
//			m_aMemory[i].bInUse= false;
//			break;
//		}
//	}
//	LeaveCriticalSection(&m_rPoolSafe);
//}
//
//void CMemPool::PoolFreeUnused()
//{
//	// Now delete the pool memory above used.
//	EnterCriticalSection(&m_rPoolSafe);
//	while (m_aMemory.size())
//	{
//		if (!m_aMemory.back().bInUse)
//		{
//			free(m_aMemory.back().pMemory);
//			m_aMemory.pop_back();
//		} else	// Stop at the first in use memory.
//			break;
//	}
//	LeaveCriticalSection(&m_rPoolSafe);
//}