#pragma once

// Only the SDK entry points used by HikCameraDevice are replaced in this test target.
// Keep the vendor's parameter structures so the production configuration code runs unchanged.
#include <CameraParams.h>
#include <MvErrorDefine.h>

extern "C" {
int __stdcall MV_CC_EnumDevices(unsigned int, MV_CC_DEVICE_INFO_LIST*);
int __stdcall MV_CC_CreateHandle(void**, const MV_CC_DEVICE_INFO*);
int __stdcall MV_CC_OpenDevice(void*, unsigned int = MV_ACCESS_Exclusive, unsigned short = 0);
int __stdcall MV_CC_CloseDevice(void*);
int __stdcall MV_CC_DestroyHandle(void*);
int __stdcall MV_CC_GetEnumValue(void*, const char*, MVCC_ENUMVALUE*);
int __stdcall MV_CC_GetEnumEntrySymbolic(void*, const char*, MVCC_ENUMENTRY*);
int __stdcall MV_CC_SetEnumValue(void*, const char*, unsigned int);
int __stdcall MV_CC_SetEnumValueByString(void*, const char*, const char*);
int __stdcall MV_CC_GetIntValueEx(void*, const char*, MVCC_INTVALUE_EX*);
int __stdcall MV_CC_SetIntValueEx(void*, const char*, int64_t);
int __stdcall MV_CC_GetFloatValue(void*, const char*, MVCC_FLOATVALUE*);
int __stdcall MV_CC_SetFloatValue(void*, const char*, float);
int __stdcall MV_CC_GetBoolValue(void*, const char*, bool*);
int __stdcall MV_CC_SetBoolValue(void*, const char*, bool);
int __stdcall MV_CC_SetImageNodeNum(void*, unsigned int);
int __stdcall MV_CC_SetGrabStrategy(void*, MV_GRAB_STRATEGY);
int __stdcall MV_CC_StartGrabbing(void*);
int __stdcall MV_CC_StopGrabbing(void*);
int __stdcall MV_CC_ClearImageBuffer(void*);
int __stdcall MV_CC_GetOneFrameTimeout(void*, unsigned char*, unsigned int, MV_FRAME_OUT_INFO_EX*, unsigned int);
int __stdcall MV_CC_GetImageBuffer(void*, MV_FRAME_OUT*, unsigned int);
int __stdcall MV_CC_FreeImageBuffer(void*, MV_FRAME_OUT*);
int __stdcall MV_CC_ConvertPixelTypeEx(void*, MV_CC_PIXEL_CONVERT_PARAM_EX*);
}
