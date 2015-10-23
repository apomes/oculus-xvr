# oculus-xvr

If you need a DLL for your graphics/VR engine to render to the Oculus DK2 in direct mode (developed with SDK 0.7), this project is for you!

This project is a DLL that integrates the Oculus Rift DK2 (SDK 0.7) and exports functions that can be used in some graphic engines (that do not allow for C++ programming but can import DLLs) to render in direct mode to the Oculus.

The DLL renders to a MSAA framebuffer and then copies it to the framebuffer for the Oculus and to a framebuffer owned by your application so you can have the rendering duplicated to your computer's screen.

Although I only tested it with XVR (vrmedia.it), you might be able to adapt it for other engines.

Cheers!
Ausiàs
