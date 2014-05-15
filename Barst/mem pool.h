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

#endif


