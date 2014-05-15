#include "stdafx.h"
#include "misc tools.h"
#include <limits.h>
#include <MMSystem.h>

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
	m_uiResolution = 1;
    while (timeBeginPeriod(m_uiResolution) == TIMERR_NOCANDO)
		if (++m_uiResolution == 20)
			break;

	ResetTimer();
}

CTimer::~CTimer()
{
    timeEndPeriod(m_uiResolution);
}

double CTimer::Seconds() const
{
    double dTime;
    if (m_llFrequency.QuadPart != 0)	// Is performance clock availible?
    {
        LARGE_INTEGER llTime;
        QueryPerformanceCounter(&llTime);     // get current time

		// Calc elapsed ticks since reset.
		if (llTime.QuadPart < m_llStart.QuadPart)	// It overflowed
			dTime = double(llTime.QuadPart+(LLONG_MAX-m_llStart.QuadPart));
		else
			dTime = double(llTime.QuadPart-m_llStart.QuadPart);
		dTime /= double(m_llFrequency.QuadPart); // Convert to secs
    }
    else
	{
		DWORD dwTime= timeGetTime();	// Current time in ms

		if (dwTime<m_llStart.LowPart)	// It overflowed
			dTime = double(dwTime+(ULONG_MAX-m_llStart.LowPart));
		else
			dTime = double(dwTime-m_llStart.LowPart);
        dTime /= 1000.0;	// Convert to secs.
	}
    return dTime;
}

double CTimer::Seconds(LARGE_INTEGER llStart) const
{
    double dTime;
    if (m_llFrequency.QuadPart != 0)	// Is performance clock availible?
    {
        LARGE_INTEGER llTime;
        QueryPerformanceCounter(&llTime);     // get current time

		// Calc elapsed ticks since reset.
		if (llTime.QuadPart < llStart.QuadPart)	// It overflowed
			dTime = double(llTime.QuadPart+(LLONG_MAX-llStart.QuadPart));
		else
			dTime = double(llTime.QuadPart-llStart.QuadPart);
		dTime /= double(m_llFrequency.QuadPart); // Convert to secs
    }
    else
	{
		DWORD dwTime= timeGetTime();	// Current time in ms

		if (dwTime<llStart.LowPart)	// It overflowed
			dTime = double(dwTime+(ULONG_MAX-llStart.LowPart));
		else
			dTime = double(dwTime-llStart.LowPart);
        dTime /= 1000.0;	// Convert to secs.
	}
    return dTime;
}

double CTimer::TimeOf(LARGE_INTEGER llTime) const
{
    double dTime;
    if (m_llFrequency.QuadPart != 0)	// Is performance clock availible?
    {
		// Calc elapsed ticks since reset.
		if (llTime.QuadPart < m_llStart.QuadPart)	// It overflowed
			dTime = double(llTime.QuadPart+(LLONG_MAX-m_llStart.QuadPart));
		else
			dTime = double(llTime.QuadPart-m_llStart.QuadPart);
		dTime /= double(m_llFrequency.QuadPart); // Convert to secs
    }
    else
	{
		if (llTime.LowPart<m_llStart.LowPart)	// It overflowed
			dTime = double(llTime.LowPart+(ULONG_MAX-m_llStart.LowPart));
		else
			dTime = double(llTime.LowPart-m_llStart.LowPart);
        dTime /= 1000.0;	// Convert to secs.
	}
    return dTime;
}

void CTimer::ResetTimer()
{	// Get most current frequency in case power states were switched etc.
	if (!QueryPerformanceFrequency(&m_llFrequency))
        m_llFrequency.QuadPart = 0;

	if (m_llFrequency.QuadPart != 0)		// get current/start time
        QueryPerformanceCounter(&m_llStart);
    else
		m_llStart.LowPart= timeGetTime();
}