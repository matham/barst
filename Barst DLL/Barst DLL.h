#ifndef CPL_BARST_DLL_H
#define CPL_BARST_DLL_H

#include <Windows.h>
#include "cpl defs.h"
// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the BARSTDLL_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// BARSTDLL_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef BARSTDLL_EXPORTS
#define BARSTDLL_API __declspec(dllexport)
#else
#define BARSTDLL_API __declspec(dllimport)
#endif

/**
 * Launches the Barst exe. 
 * @param	szPath		   	Full pathname of the file.
 * @param	szCurrDir	   	The curr dir.
 * @param	szPipe		   	The pipe.
 * @param	dwBuffSizeWrite	The buffer size write.
 * @param	dwBuffSizeRead 	The buffer size read.
 * @param	dwTimeout	   	The timeout.
 * @return	.
 */
extern "C"	BARSTDLL_API int __stdcall BarstStart(const TCHAR* szPath, const TCHAR* szCurrDir, const TCHAR* szPipe, 
	DWORD dwBuffSizeWrite, DWORD dwBuffSizeRead, DWORD dwTimeout);

/**
 * Barst end.
 * @param	szPipe   	The pipe.
 * @param	dwTimeout	The timeout.
 * @return	.
 */
extern "C"	BARSTDLL_API int __stdcall BarstEnd(const TCHAR* szPipe, DWORD dwTimeout);
extern "C"	BARSTDLL_API int __stdcall BarstOpenHandle(const TCHAR* szPipe, DWORD dwTimeout, HANDLE* phPipe);
extern "C"	BARSTDLL_API void __stdcall BarstCloseHandle(HANDLE hPipe);
extern "C"	BARSTDLL_API int __stdcall BarstGetVersion(const TCHAR* szPipe, DWORD dwTimeout, DWORD* pdwVersion);
extern "C"	BARSTDLL_API int __stdcall BarstGetManVersion(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan, DWORD* pdwVersion);
extern "C"	BARSTDLL_API int __stdcall BarstGetManDeviceInfo(const TCHAR* szPipe, DWORD dwTimeout, int nChan, SBaseOut* psInfo);
extern "C"	BARSTDLL_API int __stdcall BarstCreateManager(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan, int* pnChan);
extern "C"	BARSTDLL_API int __stdcall BarstCloseManager(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan);
extern "C"	BARSTDLL_API int __stdcall BarstCreateFTDIChan(const TCHAR* szPipe, DWORD dwTimeout, const char* szDesc, const char* szSerial, 
	const SChanInitFTDI* psChanInit, void* pChanData, DWORD dwSize, int* pnManChan, int* pnChan, FT_DEVICE_LIST_INFO_NODE_OS* psFTInfo, 
	LARGE_INTEGER *pllStart);
extern "C"	BARSTDLL_API int __stdcall BarstCreateRTVChan(const TCHAR* szPipe, DWORD dwTimeout,	SChanInitRTV* psChanInit, 
	int* pnManChan, int nChan, LARGE_INTEGER *pllStart);
extern "C"	BARSTDLL_API int __stdcall BarstCreateSerialChan(const TCHAR* szPipe, DWORD dwTimeout,	const SChanInitSerial* psChanInit, 
	int* pnManChan, int* pnChan, LARGE_INTEGER *pllStart);
extern "C"	BARSTDLL_API int __stdcall BarstCloseChan(const TCHAR* szPipe, DWORD dwTimeout, enum EQueryType eMan, int nChan);
extern "C"	BARSTDLL_API int __stdcall BarstSetDeviceStatus(const TCHAR* szPipe, DWORD dwTimeout, int nChan, bool bActivate);
extern "C"	BARSTDLL_API int __stdcall BarstSetDeviceStatusTrigger(HANDLE hPipe, int nChan, bool bActivate);
extern "C"	BARSTDLL_API int __stdcall BarstFTDIWriteMultiData(HANDLE hPipe, int nChan, const SValveData* aValveData, DWORD dwCount, 
	double* pdTime);
extern "C"	BARSTDLL_API int __stdcall BarstFTDIWritePinData(HANDLE hPipe, int nChan, const SPinWData* asData, const unsigned char* aucBufData, 
	DWORD dwCount, double* pdTime);
extern "C"	BARSTDLL_API int __stdcall BarstSendDeviceTrigger(HANDLE hPipe, int nChan);
extern "C"	BARSTDLL_API int __stdcall BarstFTDIReadADCData(HANDLE hPipe, SADCData* psADC, DWORD* adwChan1Data, DWORD* adwChan2Data, 
	DWORD dwChanSize);
extern "C"	BARSTDLL_API int __stdcall BarstFTDIReadMultiData(HANDLE hPipe, bool* abData, DWORD dwCount, double* pdTime);
extern "C"	BARSTDLL_API int __stdcall BarstFTDIReadPinData(HANDLE hPipe, unsigned char* aucData, DWORD dwCount, double* pdTime);
extern "C"	BARSTDLL_API int __stdcall BarstRTVReadImageData(HANDLE hPipe, unsigned char* aucData, DWORD dwSize, double* pdTime);
extern "C"	BARSTDLL_API int __stdcall BarstSerialWrite(HANDLE hPipe, int nChan, SSerialData* psData, const char* acData, double* pdTime);
extern "C"	BARSTDLL_API int __stdcall BarstSerialRead(HANDLE hPipe, int nChan, SSerialData* psData, char* acData, double* pdTime);
extern "C"	BARSTDLL_API double __stdcall BarstCurrentTime(LARGE_INTEGER llStart);


#endif