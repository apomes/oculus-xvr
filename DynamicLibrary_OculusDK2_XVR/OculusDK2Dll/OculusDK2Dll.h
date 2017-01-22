// OculusDK2Dll.h

// Author: Ausias Pomes
// Date: 07/10/2015

#ifndef VALIDATE
#define VALIDATE(x, msg) if (!(x)) { MessageBoxA(NULL, (msg), "OculusRoomTiny", MB_ICONERROR | MB_OK); exit(-1); }
#endif

// Rift functions
extern "C" __declspec(dllexport) int OVR_Initialize();
extern "C" __declspec(dllexport) float OVR_Create();
extern "C" __declspec(dllexport) int OVR_GetScreenResolution(int &HRes, int &VRes);
extern "C" __declspec(dllexport) int OVR_GetInterpupillaryDistance(float &IPD);
extern "C" __declspec(dllexport) int OVR_GetInterpupillaryDistance(float &IPD);

extern "C" __declspec(dllexport) int OVR_Destroy();
extern "C" __declspec(dllexport) int OVR_ConfigureTracking();
extern "C" __declspec(dllexport) int OVR_GetTrackingState();
extern "C" __declspec(dllexport) int OVR_GetSensorPredictedOrientation(float &qW, float &qX, float &qY, float &qZ);
extern "C" __declspec(dllexport) int OVR_GetSensorPredictedPosition(float &vX, float &vY, float &vZ);
extern "C" __declspec(dllexport) int OVR_CreateSwapTextureSetGL();
extern "C" __declspec(dllexport) int OVR_CreateSwapTextureSetGLWithMSAASamples(int aNumSamples);
extern "C" __declspec(dllexport) int OVR_PrepareFrameRendering();
extern "C" __declspec(dllexport) int OVR_PrepareOGLContext();
extern "C" __declspec(dllexport) int OVR_CleanOGLContext();
extern "C" __declspec(dllexport) int OVR_SubmitFrame();
extern "C" __declspec(dllexport) void OVR_SetMultisampleAA(int isMultisampleOn);

int SetAndClearRenderSurface();
void UnsetRenderSurface();