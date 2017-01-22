// OculusDK2Dll.cpp : Defines the exported functions for the DLL application. 
// This DLL is a wrapper for the Oculus SDK to be accessed from game engines like XVR.

// Author: Ausias Pomes
// Date: 07/10/2015



#include "stdafx.h"
#include <stdexcept>
#include <string>
#include <iostream>

#include "Win32_GLAppUtil.h"
#include <Kernel/OVR_System.h>

#include "OculusDk2Dll.h"


using namespace OVR;
using namespace std;

OVR::GLEContext GLEContexto;

ovrHmd HMD;
ovrGraphicsLuid luid;
ovrHmdDesc hmdDesc;
ovrTrackingState trackingState;

ovrSwapTextureSet*  TextureSet;
ovrSizei idealTextureSizeSet[2];

// Size of the buffer for the Oculus
Sizei bufferSize;

// Rect for the XVR viewport
GLint XVRViewportRect[4] = {};

ovrVector3f ViewOffset[2];
ovrPosef EyeRenderPose[2];
ovrEyeRenderDesc EyeRenderDesc[2];
ovrVector3f      hmdToEyeViewOffset[2];
ovrLayerEyeFov layer;

// MSAA fbo
GLuint fboMsaaId;
GLuint numSamples = 4;
GLuint depthMsaaId; // Attachment point for depth MSAA render buffer
// Oculus fbo
GLuint fboOculusId;
// XVR fbo
GLint fboXvrId;

// Textures
GLuint depthTexId;
GLuint msaaTexId;

// MSAA or not
bool useMSAA = false;


int OVR_Initialize() {
	OVR::System::Init();

	ovrResult result = ovr_Initialize(nullptr);
	VALIDATE(OVR_SUCCESS(result), "Failed to initialize libOVR.");

	return result;
}

float OVR_Create() {
	ovrResult result = ovr_Create(&HMD, &luid);
	hmdDesc = ovr_GetHmdDesc(HMD);
	return hmdDesc.DefaultEyeFov[0].UpTan;
}

int OVR_GetScreenResolution(int &HRes, int &VRes) {
	HRes = hmdDesc.Resolution.w;
	VRes = hmdDesc.Resolution.h;
	return 0;
}

int OVR_GetInterpupillaryDistance(float &IPD) {
	IPD = (float)0.064;
	return 0;
}

int OVR_Destroy() {
	// Destroy swap texture set
	if (TextureSet) {
		ovr_DestroySwapTextureSet(HMD, TextureSet);
		TextureSet = nullptr;
	}
	// TODO: destroy MSAA texture
	// ...

	if (fboMsaaId) {
		glDeleteFramebuffers(1, &fboMsaaId);
		fboMsaaId = 0;
	}
	if (fboOculusId) {
		glDeleteFramebuffers(1, &fboOculusId);
		fboOculusId = 0;
	}

	// Destroy and shutdown HMD
	ovr_Destroy(HMD);
	ovr_Shutdown();
	OVR::System::Destroy();
	return 0;
}

int OVR_ConfigureTracking() {
	ovrResult result = ovr_ConfigureTracking(HMD, ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection | ovrTrackingCap_Position, 0);
	return result;
}

// Retrieves the tracking state from the Oculus Rift
int OVR_GetTrackingState() {
	ovrFrameTiming ftiming = ovr_GetFrameTiming(HMD, 0);
	trackingState = ovr_GetTrackingState(HMD, ftiming.DisplayMidpointSeconds);
	return 0;
}

// Retrieves the orientation of the Oculus Rift as a quaternion
int OVR_GetSensorPredictedOrientation(float &qW, float &qX, float &qY, float &qZ) {
	qW = trackingState.HeadPose.ThePose.Orientation.w;
	qX = trackingState.HeadPose.ThePose.Orientation.x;
	qY = trackingState.HeadPose.ThePose.Orientation.y;
	qZ = trackingState.HeadPose.ThePose.Orientation.z;
	return 0;
}

// Retrieves the position of the Oculus Rift 
int OVR_GetSensorPredictedPosition(float &vX, float &vY, float &vZ) {
	vX = trackingState.HeadPose.ThePose.Position.x;
	vY = trackingState.HeadPose.ThePose.Position.y;
	vZ = trackingState.HeadPose.ThePose.Position.z;
	return 0;
}

int OVR_CreateSwapTextureSetGL() {
	ovrResult result;
	for (int eye = 0; eye < 2; ++eye) {
		idealTextureSizeSet[eye] = ovr_GetFovTextureSize(HMD, ovrEyeType(eye), hmdDesc.DefaultEyeFov[eye], 1);
	}

	bufferSize.w = idealTextureSizeSet[0].w + idealTextureSizeSet[1].w;
	bufferSize.h = max(idealTextureSizeSet[0].h, idealTextureSizeSet[1].h);
	
	// Init OpenGL context
	OVR::GLEContext::SetCurrentContext(&GLEContexto);
	GLEContexto.Init();

	////////////////////////////////////
	// Allocate the "eye render buffer" in a 2D texture
	result = ovr_CreateSwapTextureSetGL(HMD, GL_SRGB8_ALPHA8, bufferSize.w, bufferSize.h, &TextureSet);
	
	for (int i = 0; i < TextureSet->TextureCount; ++i)
	{
		ovrGLTexture* tex = (ovrGLTexture*)&TextureSet->Textures[i];
		glBindTexture(GL_TEXTURE_2D, tex->OGL.TexId); // id for texture unit 1 and 2

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	

	////////////////////////////////////
	// Depth texture (MSAA)
	glGenRenderbuffers(1, &depthMsaaId);
	glBindRenderbuffer(GL_RENDERBUFFER, depthMsaaId);
	// Add depth/stencil buffer to MSAA fbo
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, numSamples, GL_DEPTH24_STENCIL8, bufferSize.w, bufferSize.h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthMsaaId);

	////////////////////////////////////
	// MSAA 2D texture
	glGenTextures(1, &msaaTexId);

	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaaTexId);
	glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, numSamples, GL_SRGB8_ALPHA8, bufferSize.w, bufferSize.h, GL_TRUE);

	// Start with MSAA disabled
	glDisable(GL_MULTISAMPLE);


	////////////////////////////////////
	// Depth texture (no MSAA)
	glGenTextures(1, &depthTexId);
	glBindTexture(GL_TEXTURE_2D, depthTexId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	GLenum internalFormat = GL_DEPTH_COMPONENT24;
	GLenum type = GL_UNSIGNED_INT;
	if (GLE_ARB_depth_buffer_float)
	{
		internalFormat = GL_DEPTH_COMPONENT32F;
		type = GL_FLOAT;
	}
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, bufferSize.w, bufferSize.h, 0, GL_DEPTH_COMPONENT, type, NULL);
	

	// Instead of rendering in an FBO in XVR, we setup an FBO right here, for Msaa and for the Oculus
	glGenFramebuffers(1, &fboMsaaId);
	glGenFramebuffers(1, &fboOculusId);

	// Get XVR fbo ID which is currently bound
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fboXvrId);

	return result;
}

// Like OVR_CreateSwapTextureSetGL but we can specify the number of samples for MSAA
// Default number of samples for MSAA is 4
int OVR_CreateSwapTextureSetGLWithMSAASamples(int aNumSamples) {
	numSamples = aNumSamples;
	return OVR_CreateSwapTextureSetGL();
}

int OVR_PrepareFrameRendering() {
	// Initialize VR structures, filling out description.
	EyeRenderDesc[0] = ovr_GetRenderDesc(HMD, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
	EyeRenderDesc[1] = ovr_GetRenderDesc(HMD, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);
	hmdToEyeViewOffset[0] = EyeRenderDesc[0].HmdToEyeViewOffset;
	hmdToEyeViewOffset[1] = EyeRenderDesc[1].HmdToEyeViewOffset;

	// Turn off vsync to let the compositor do its magic
	wglSwapIntervalEXT(0);

	// Initialize our single full screen Fov layer.
	layer.Header.Type = ovrLayerType_EyeFov;
	layer.Header.Flags = ovrLayerFlag_TextureOriginAtBottomLeft;   // Because OpenGL.
	layer.ColorTexture[0] = TextureSet;
	layer.ColorTexture[1] = TextureSet;
	layer.Fov[0] = EyeRenderDesc[0].Fov;
	layer.Fov[1] = EyeRenderDesc[1].Fov;
	layer.Viewport[0] = Recti(0, 0, bufferSize.w / 2, bufferSize.h);
	layer.Viewport[1] = Recti(bufferSize.w / 2, 0, bufferSize.w / 2, bufferSize.h);
	// ld.RenderPose is updated later per frame.

	return 0;
}

int OVR_PrepareOGLContext() {
	// Get eye poses, feeding in correct IPD offset
	ViewOffset[0] = EyeRenderDesc[0].HmdToEyeViewOffset;
	ViewOffset[1] = EyeRenderDesc[1].HmdToEyeViewOffset;

	// NOTE: We have to use the same tracking state that we use to render the scene!! 
	// We might otherwise introduce lag...
	ovr_CalcEyePoses(trackingState.HeadPose.ThePose, ViewOffset, EyeRenderPose);

	// Increment to use next texture, just before writing
	TextureSet->CurrentIndex = (TextureSet->CurrentIndex + 1) % TextureSet->TextureCount;

	auto tex = reinterpret_cast<ovrGLTexture*>(&TextureSet->Textures[TextureSet->CurrentIndex]);

	// Update XVR fbo viewport size
	glBindFramebuffer(GL_FRAMEBUFFER, fboXvrId);
	glGetIntegerv(GL_VIEWPORT, XVRViewportRect);

	// Set Oculus fbo
	glBindFramebuffer(GL_FRAMEBUFFER, fboOculusId);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->OGL.TexId, 0);

	if (useMSAA)
	{
		// Prepare MSAA fbo for rendering
		glBindFramebuffer(GL_FRAMEBUFFER, fboMsaaId);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, msaaTexId, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthMsaaId);
	}
	else
	{
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexId, 0);
	}
	
	

	// Change viewport to the size of the Oculus fbo
	glViewport(0, 0, bufferSize.w, bufferSize.h);

	// Clean MSAA buffer
	glClearColor(0, 0.25, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_FRAMEBUFFER_SRGB);

	return TextureSet->CurrentIndex;
}

int OVR_CleanOGLContext() {
	if (useMSAA)
	{
		// Blit MSAA texture to the Oculus FBO
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboOculusId);   // Set the Oculus buffer as the draw buffer
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fboMsaaId); // Make multisampled FBO the read framebuffer
		glBlitFramebuffer(0, 0, bufferSize.w, bufferSize.h, 0, 0, bufferSize.w, bufferSize.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		// Blit MSAA texture to the back buffer so it shows in the XVR FBO
		glViewport(0, 0, XVRViewportRect[2], XVRViewportRect[3]); // Change viewport to the size of the XVR window
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboXvrId); // Set XVR buffer as the draw buffer for the mirroring
		// Now we read from the Oculus buffer since reading from the MSAA buffer and blitting into a buffer of a different size
		// gives an error
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fboOculusId);
		glBlitFramebuffer(0, 0, bufferSize.w, bufferSize.h, 0, 0, XVRViewportRect[2], XVRViewportRect[3], GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}
	else
	{
		// Blit MSAA texture to the back buffer so it shows in the XVR FBO
		glViewport(0, 0, XVRViewportRect[2], XVRViewportRect[3]); // Change viewport to the size of the XVR window
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboXvrId); // Set XVR buffer as the draw buffer for the mirroring
		// Now we read from the Oculus buffer since reading from the MSAA buffer and blitting into a buffer of a different size
		// gives an error
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fboOculusId);
		glBlitFramebuffer(0, 0, bufferSize.w, bufferSize.h, 0, 0, XVRViewportRect[2], XVRViewportRect[3], GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	
	// Some clean up
	glBindFramebuffer(GL_FRAMEBUFFER, fboXvrId); // Set the XVR buffer as the draw buffer
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
	return 0;
}

int OVR_SubmitFrame() {
	// Set up positional data.
	ovrViewScaleDesc viewScaleDesc;
	viewScaleDesc.HmdSpaceToWorldScaleInMeters = 1.0f;
	viewScaleDesc.HmdToEyeViewOffset[0] = ViewOffset[0];
	viewScaleDesc.HmdToEyeViewOffset[1] = ViewOffset[1];

	layer.RenderPose[0] = EyeRenderPose[0];
	layer.RenderPose[1] = EyeRenderPose[1];

	ovrLayerHeader* layers = &layer.Header;
	ovrResult result = ovr_SubmitFrame(HMD, 0, &viewScaleDesc, &layers, 1);

	return result;
}

void OVR_SetMultisampleAA(int isMultisampleOn) {
	if (isMultisampleOn) {
		useMSAA = true;
		glEnable(GL_MULTISAMPLE);
	}
	else {
		useMSAA = false;
		glDisable(GL_MULTISAMPLE);
	}
}
