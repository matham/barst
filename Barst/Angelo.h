/*********************************************************************/
/* File: Angelo.h                                                    */
/*********************************************************************/

#if !defined(__Angelo)
#define __Angelo


#ifdef  __cplusplus
extern "C" {
#endif

// Category: System
I16 PASCAL AngeloRTV_Initial(U16 PortNo);
I16 PASCAL AngeloRTV_Close(U16 PortNo);
I16 PASCAL AngeloRTV_Software_Reset(U16 PortNo);
U16 PASCAL AngeloRTV_Read_Serial(U16 CardNo, U32* HighByte, U32* LowByte); //Serial Code
I16 PASCAL AngeloRTV_Get_Version(U32 *DriverVersion,U32 *DLLVersion,U32 *Reserved);

// Category: Configuration
I16 PASCAL AngeloRTV_Set_Image_Config(U16 PortNo,U8 ConfigIndex ,U8 Value);
I16 PASCAL AngeloRTV_Get_Image_Config(U16 PortNo,U8 ConfigIndex ,U8* Value);
I16 PASCAL AngeloRTV_Set_Color_Format(U16 PortNo,U8 ColorFormat) ;
I16 PASCAL AngeloRTV_Get_Color_Format(U16 PortNo,U8 *ColorFormat);
I16 PASCAL AngeloRTV_Set_Video_Format(U16 PortNo,U8 Value);
I16 PASCAL AngeloRTV_Get_Video_Format(U16 PortNo,U8 *Value);
I16 PASCAL AngeloRTV_Set_Image_Geometric(U16 PortNo,U32 X_Offset,U32 Y_Offset,U32 X_Active,U32 Y_Active,double X_Scale,double Y_Scale);
I16 PASCAL AngeloRTV_Detect_Video_Format(U16 PortNo,U8 *Value);

// Category: Image Grabbing
I16 PASCAL AngeloRTV_Capture_Start(U16 PortNo, U32 CaptureNo);
I16 PASCAL AngeloRTV_Capture_Stop (U16 PortNo);
I16 PASCAL AngeloRTV_Select_Channel(U16 PortNo,U16 Multiplex);
I16 PASCAL AngeloRTV_Capture_Config(U16 PortNo, U32 Start_Field);
I16 PASCAL AngeloRTV_Sync_Grab(U16 PortNo, U32* Start_Address, U32* Width, U32* Height, U32* Size_Byte);

// Category: GPIO & EPROM
I16 PASCAL AngeloRTV_Set_GPIO_Sts(U16 PortNo,U8 Status);
I16 PASCAL AngeloRTV_Get_GPIO_Sts(U16 PortNo,U8* Status);
I16 PASCAL AngeloRTV_Write_EEPROM(U16 CardNo, U8 Address, U8 Value);
I16 PASCAL AngeloRTV_Read_EEPROM(U16 CardNo, U8 Address, U8 *Value);
I16 PASCAL AngeloRTV_Set_GPIO_Int_Logic(U16 PortNo,U16 Logic);
//cPci RTV24 only
I16 PASCAL AngeloRTV_Set_LED_Sts(U16 PortNo,U8 Status);

// Category: Callback & Thread
I16 PASCAL AngeloRTV_Set_Callback (U16 PortNo, void ( __stdcall *CallBackProc)(void *VideoBufferaddress ,U16 PortNo));
I16 PASCAL AngeloRTV_Set_Int_Event(U16 PortNo,HANDLE* hEvent);
I16 PASCAL AngeloRTV_Get_Int_Status(U16 PortNo,U32 *IntStatus);

// Category: Watch dog
I16 PASCAL AngeloRTV_Set_WDT(U16 CardNo,U16 Enable,U16 Interval);

// Category: Software Trigger
I16 PASCAL AngeloRTV_Trigger_Config(U16 PortNo,U16 Interval);
I16 PASCAL AngeloRTV_Trigger_Start(U16 CardNo,U16 Multiplex);

// Category: Frame Buffer
I16 PASCAL AngeloRTV_Get_Frame(U16 PortNo,U32* Start_Address,U32* Width,U32* Height,U32* Size_Byte);
I16 PASCAL AngeloRTV_Save_File(U16 PortNo, char* FileName,U8 FileFormat,int nQuality);//modify 05/06/03, add the nQuality, only for jpeg
I16 PASCAL AngeloRTV_Copy_Frame(U16 PortNo,U8 *Dest_Address,U32 Size_Byte);


#ifdef  __cplusplus
}
#endif

#endif // !defined(AFX_HSL_IO_H__29FD7241_6408_11D6_8A27_0030640119BE__INCLUDED_)
