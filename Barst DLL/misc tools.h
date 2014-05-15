/** Defines misc functions used in the project. */


#ifndef MISC_TOOLS_H
#define MISC_TOOLS_H


#include <Windows.h>

#define PRAGMA_STR1(x) #x
#define PRAGMA_STR2(x) PRAGMA_STR1 (x)
#define NOTE(x)  message (__FILE__ "(" PRAGMA_STR2(__LINE__) ") : -NOTE- " #x)


/**
	Loads a dll as well as a list of functions in the dll.
//GetProcAddresses
//Argument1: hLibrary - Handle for the Library Loaded
//Argument2: lpszLibrary - Library to Load
//Argument3: nCount - Number of functions to load
//[Arguments Format]
//Argument4: Function Address - Function address we want to store
//Argument5: Function Name -  Name of the function we want
//[Repeat Format]
//
//Returns: FALSE if failure
//Returns: TRUE if successful */
BOOL GetProcAddresses(HINSTANCE *hLibrary, LPCTSTR lpszLibrary, INT nCount, ... );


/** Defines a high precision timer. Because of overflow, we allow the timer to be reset so that
	we count time relative to when it was last reset as opposed to when the computer was last
	started etc. The time it takes to overflow in this case can vary depending on the precision
	of the timer, in upwards of about a month. Since we return time relative to some other time,
	we can use the time of that starting point in other instances of this class to calculate
	time raltive to that offset. 
	
	This class can use either the QueryPerformanceCounter() or the timeGetTime() function.
	The assumption implicit in the Seconds(LARGE_INTEGER llStart) and TimeOf(LARGE_INTEGER llTime)
	functions is that the instance used to generate the llStart or llTime value used the same
	time function as this instance otherwise they would be incompatible. The reason this 
	should work is that if the performance counter isn't availible in one instance it shouldn't
	be availible for the other as well. */
class CTimer
{
public:
	/** Initializes timer and calls ResetTimer(). */
    CTimer();
    virtual ~CTimer();

	/** Returns the current time in seconds as elapsed from the last time ResetTimer()
		was called on this object. This is only accurate until overflow. */
    double Seconds() const;
	/** Returns the currect time relative to an offset of some other clock. That is,
		You get the time when RestTimer() was calles on another instance of the timer
		using the GetStart() function and when you pass in that large integer to this 
		function, the function will return the time relative to that offset. */
	double Seconds(LARGE_INTEGER llStart) const;
	/** Returns the time difference between llTime and the time that the timer was last reset.
		llTime would typically be the offset time when another timer instance was reset.
		You can use this to calculate the time difference between two timers so as to find
		a consistent time. */
	double TimeOf(LARGE_INTEGER llTime) const;
	/** Returns the offset, i.e. the internal time when reset was last called. */
	LARGE_INTEGER GetStart() const { return m_llStart;}
	/** Resets the elpased time count to the current time. I.e. Seconds() returns the elapsed
		time since this function was last called. */
	void ResetTimer();
private:
    LARGE_INTEGER   m_llFrequency;          // Performance counter frequency or zero
	LARGE_INTEGER	m_llStart;				// The value of the performance counter when reset was called.
    DWORD           m_uiResolution;         // Resolution for multimedia system time
};

#endif