/** Defines a mempry pool which allowes caching of memory. Currently just calls malloc() and free(). */

#ifndef	_CPL_MEM_POOL_H_
#define	_CPL_MEM_POOL_H_


#include <Windows.h>
#include <vector>

typedef struct SMemoryItem
{
	/** Pointer to the memory. */
	void*				pMemory;
	/** Size of the memory in bytes at pMemory. */
	unsigned long long	ullSize;
	/** Indicates if this memory is currently being used.*/
	bool				bInUse;

	SMemoryItem()
	{
		memset(&pMemory, 0, sizeof(SMemoryItem));
	}
} SMemoryItem;



class CMemPool
{
public:
	CMemPool(){};
	~CMemPool(){};

	void* PoolAcquire(unsigned long long ullSize) {return malloc((size_t)ullSize);}
	void  PoolRelease(void* pHead) {free(pHead);}
	void  PoolFreeUnused(){};
private:

	//std::vector<SMemoryItem>	m_aMemory;
	//CRITICAL_SECTION			m_rPoolSafe;
};


/** Item used in CMemRing. **/
typedef struct SRingItem
{
	/** Pointer to the memory. */
	void*				pMemory;
	/** The number of users that claimed the memory. */
	int					nCount;
} SRingItem;



/** A class which provides memory pointers and allows users to claim
a pointer. If a memory items is claimed, that memory item is not returned
until all that claimed it, release it. Multiple users can claim the
same item. If there's no unclaimed memory, it creates some.**/
class CMemRing
{
public:
	/** ll_size is the size that the memory blocks will be. **/
	CMemRing(__int64 ll_size, int nMinElems, int nMaxElems);
    virtual ~CMemRing();

	/** Takes a index to memory item, returns the memory pointer, and adds
	a claim to the pointer. **/
	void *GetIndexMemory(int nIdx);
	/** Takes a index to memory item, returns the memory pointer, however it does not add
	a claim to the pointer.**/
	void *GetIndexMemoryUnsafe(int nIdx);
	/** Releases a claim to a memory item. **/
	void ReleaseIndex(int nIdx);
	/** Returns a pointer of memory that is uncalimed. Creates one if
	it cannot find any. pnIdx gets the index value of the memory. **/
	void *GetFree(int *pnIdx);

	/** The size of the memory blocks. **/
	const __int64			m_llSize;
	/** The minimum number of elements m_apMemory can ever have. **/
	const int				m_nMinElems;
	/** The maximum number of elements m_apMemory can ever have. **/
	const int				m_nMaxElems;
private:
	std::vector<SRingItem>		m_apMemory;		// holds the memory
	CRITICAL_SECTION			m_hMemSafe;			// protects access to the memory.
};

#endif


