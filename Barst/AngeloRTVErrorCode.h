#ifndef AngeloRTVErroeCode_h_124124_1243124324fs_ewr324r23qf_4sdF_213r
#define  AngeloRTVErroeCode_h_124124_1243124324fs_ewr324r23qf_4sdF_213r

//Return Error Code
#define ERROR_NoError				0

// Card Not Found (-1) ~ -(10)
#define ERROR_PCIBios_Not_Exist				-1
#define ERROR_Card_Not_Exist				-2	
#define ERROR_Card_Not_Accessible			-3
#define ERROR_UnKnow_Card_Type				-4
#define ERROR_Maximum_Number_Of_Card_Exceed	-5
#define ERROR_Event_Allocate_Failed			-6
#define ERROR_Not_Initialized				-7
#define ERROR_Failed_To_Create_INT_Thread	-8
#define	ERROR_Invalid_PortNo				-9
#define	ERROR_Invaild_Video_Format			-10
#define	ERROR_Invalid_Color_Format			-11
#define	ERROR_CPLD_Check_Failed				-12
#define ERROR_Invalid_Frame_Rate			-13
#define ERROR_Open_File_Fault				-14
#define ERROR_Invalid_Address				-15



#define ERROR_Invalid_Image_Offset			-100
#define ERROR_Invalid_Image_Size			-101
#define ERROR_Invalid_Image_Scale			-102
#define ERROR_No_Signal						-103

#define ERROR_Image_In_Processing			-200
#define ERROR_Already_Initialized			-201
#endif