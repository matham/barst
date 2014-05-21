#include "stdafx.h"
#include "misc tools.h"
#include <limits.h>
#include <MMSystem.h>

/** Global timer. **/
CTimer g_cTimer;


BOOL GetProcAddresses(HINSTANCE *hLibrary, LPCTSTR lpszLibrary, INT nCount, ... )
{
    va_list va;
    va_start( va, nCount );

    if ( ( *hLibrary = LoadLibrary( lpszLibrary ) ) != NULL )
    {
        FARPROC * lpfProcFunction = NULL;
        LPSTR lpszFuncName = NULL;
        INT nIdxCount = 0;
        while ( nIdxCount < nCount )
        {
            lpfProcFunction = va_arg( va, FARPROC* );
            lpszFuncName = va_arg( va, LPSTR );
            if ( ( *lpfProcFunction = 
                GetProcAddress( *hLibrary, 
                    lpszFuncName ) ) == NULL )
            {
                lpfProcFunction = NULL;
                return FALSE;
            }
            nIdxCount++;
        }
    }
    else
    {
        va_end( va );
        return FALSE;
    }
    va_end( va );
    return TRUE;
}



CTimer::CTimer()
{
	ResetTimer();
}

CTimer::~CTimer()
{
}

double CTimer::Seconds() const
{
    LARGE_INTEGER llTime;
    QueryPerformanceCounter(&llTime);     // get current time

	// Calc elapsed ticks since reset.
	if (llTime.QuadPart < m_llStart.QuadPart)	// It overflowed
		llTime.QuadPart = (llTime.QuadPart + (LLONG_MAX - m_llStart.QuadPart));
	else
		llTime.QuadPart = (llTime.QuadPart - m_llStart.QuadPart);
    return llTime.QuadPart * (1.0 / m_llFrequency.QuadPart); // Convert to secs
}

double CTimer::Seconds(LARGE_INTEGER llStart) const
{
    LARGE_INTEGER llTime;
    QueryPerformanceCounter(&llTime);     // get current time

	// Calc elapsed ticks since reset.
	if (llTime.QuadPart < llStart.QuadPart)	// It overflowed
		llTime.QuadPart = (llTime.QuadPart + (LLONG_MAX - llStart.QuadPart));
	else
		llTime.QuadPart = (llTime.QuadPart - llStart.QuadPart);
    return llTime.QuadPart * (1.0 / m_llFrequency.QuadPart); // Convert to secs
}

double CTimer::TimeOf(LARGE_INTEGER llTime) const
{
	// Calc elapsed ticks since reset.
	if (llTime.QuadPart < m_llStart.QuadPart)	// It overflowed
		llTime.QuadPart = (llTime.QuadPart + (LLONG_MAX - m_llStart.QuadPart));
	else
		llTime.QuadPart = (llTime.QuadPart - m_llStart.QuadPart);
    return llTime.QuadPart * (1.0 / m_llFrequency.QuadPart); // Convert to secs
}

void CTimer::ResetTimer()
{	// Get most current frequency in case power states were switched etc.
	QueryPerformanceFrequency(&m_llFrequency);
    QueryPerformanceCounter(&m_llStart);	// get current/start time
}
