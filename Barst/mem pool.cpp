#include "mem pool.h"




CMemRing::CMemRing(__int64 ll_size, int nMinElems, int nMaxElems) : m_llSize(ll_size),
	m_nMinElems(nMinElems), m_nMaxElems(nMaxElems)
{
	SRingItem sRingItem;
	InitializeCriticalSection(&m_hMemSafe);
	for (int i = 0; i < m_nMinElems; ++i)
	{
		sRingItem.nCount = 0;
		sRingItem.pMemory = malloc(m_llSize);
		m_apMemory.push_back(sRingItem);
	}
}

CMemRing::~CMemRing()
{
	DeleteCriticalSection(&m_hMemSafe);
	for (int i = 0; i < m_apMemory.size(); ++i)
		free(m_apMemory[i].pMemory);
}

void *CMemRing::GetIndexMemory(int nIdx)
{
	void * pHead = NULL;
	EnterCriticalSection(&m_hMemSafe);
	if (nIdx < m_apMemory.size())
	{
		pHead = m_apMemory[nIdx].pMemory;
		m_apMemory[nIdx].nCount += 1;
	}
	LeaveCriticalSection(&m_hMemSafe);
	return pHead;
}

void *CMemRing::GetIndexMemoryUnsafe(int nIdx)
{
	void * pHead = NULL;
	EnterCriticalSection(&m_hMemSafe);
	if (nIdx < m_apMemory.size())
		pHead = m_apMemory[nIdx].pMemory;
	LeaveCriticalSection(&m_hMemSafe);
	return pHead;
}

void CMemRing::ReleaseIndex(int nIdx)
{
	EnterCriticalSection(&m_hMemSafe);
	if (nIdx < m_apMemory.size())
		m_apMemory[nIdx].nCount -= 1;
	LeaveCriticalSection(&m_hMemSafe);
}

void *CMemRing::GetFree(int *pnIdx)
{
	void *pHead = NULL;
	SRingItem sRingItem;
	EnterCriticalSection(&m_hMemSafe);
	for (int i = 0; i < m_apMemory.size(); ++i)
	{
		if (!m_apMemory[i].nCount)
		{
			*pnIdx = i;
			pHead = m_apMemory[i].pMemory;
			break;
		}
	}

	if (!pHead && m_apMemory.size() < m_nMaxElems)		// we need to create new memory
	{
		sRingItem.nCount = 0;
		pHead = sRingItem.pMemory = malloc(m_llSize);
		*pnIdx = (int)m_apMemory.size();
		m_apMemory.push_back(sRingItem);
	}
	LeaveCriticalSection(&m_hMemSafe);
	return pHead;
}


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