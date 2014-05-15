/** Defines a log format. Currently not implemented. */

#ifndef	_LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <Windows.h>
#include "cpl defs.h"


/**	A ring buffer type class that holds a list of text items.
	You can store LOG_LENGTH items in the array and the length of each item
	is of maximum LOG_TEXT_LENGTH. 
	
	Every time an item is added a line number and time of add
	is appended to it at the start. Line number is a 64 bit int so it will overflow.
	These line numbers start from 1 as zero would indicate there is no 
	error to return in a line.

	Also, if you don't retrieve the log periodically since old log items will simply be
	overwritten. 
	Class is completly thread safe.*/
class CLogBuffer
{
private:
	char				m_achQueue[128][128];	// Buffer holding the log text
	CRITICAL_SECTION	m_rLogLock;		// Protects writing and reading from log
	int					m_nLogLine;		// The line number of the next item to be appended . 
	// Note: if empty, end must be before start and can't overlap otherwise counts as one item in buffer
	int					m_nStart;		// Position of first valid log if any
	int					m_nEnd;			// Position of last valid log if any
	int		m_nSize;		// Number of log items in buffer
public:
	CLogBuffer(){};
	virtual ~CLogBuffer(){};

	/** Function to add item to the log. The function appends a line number and
		time added at the start of the item.
		return: The line number appended to the item added.*/
	int Add(const TCHAR *szItem){return 0;}
	/**	Copies and deletes the min of nToWrite and # of log items available from the log into szDest. It's assumed that the length 
		of szDest is at least nToWrite*LOG_TEXT_LENGTH. 
		szDest: Where the log will be copied into. Each LOG_TEXT_LENGTH section of the array holds a null terminated string. 
			If the log item is smaller than LOG_TEXT_LENGTH, then the rest is filled up with zeros.
		nToWrite: Number of items/lines to move.
		nWrote: Number of items actually written, it's the minimum of nToWrite and the number of items currently in the log.*/
	void MoveLog(char szDest[][128], int nToWrite, int* pnWrote){};
	/** The number of items/lines currently in the log.*/
	int GetSize(){return 0;}
	/** Empties and resets the log. */
	void ClearLog(){};
};


#endif