# esUtil.c for Linux DRM

The book "Open GL ES 3.0 Programming Guide" by
Dan Ginsburg et al is a standard text for OpenGL ES.
It uses a common API to deal with the lower levels
of interfacing OpenGL ES with the actual platform
(X11, Windows, etc). The programmer uses esUtil.c,
but there are files underneath such as esUtil_X11.c,
esUtil_win32.c.

The Raspberry Pi 4 has moved from the proprietary Video Core IV
GPU interface to the standard Linux DRM
(Direct Rendering Manager) interface. There is no
esUtil_DRM.c. This project gives a first cut
at esUtil_DRM.c.

## Building for DRM

The requirement is to build the library libCommon.a
containing the files
+ esShader.c.o
+ esShapes.c.o
+ esTransform.c.o
+ esUtil.c.o
+ esUtil_DRM.c.o

It is assumed you have downloaded the program files for the book
into directory X. In subdirectory X/Common is the file
CMakeLists.txt. This needs to be replaced with the file
CMakeLists.txt from this project. The new file just adds
a new target for the DRM version.

Also replace the file esUtil.c with esUtil.c from this
project. The new version just adds some tests for
the display and surface to be non-NULL before
creating them. The DRM version will already have done that.

Then create a subdirectory X/Common/DRM/ and add the files
esUtil_DRM.c, common.h, drm-common.h.

The DRM version of libCommon.a can then be built using cmake from a
build directory by adding the flag "-DUseDRM=1" in whatever the
build directory is

    cmake -DUseDRM=1 ...

or for debugging code,

   cmake -D UseDRM=1  -DCMAKE_BUILD_TYPE=Debug ...

followed by

    make
    
## Caveat

This has only been tested on the Raspberry Pi 4, running without
X11.

The programs in Chapters 2, 6, 9, 10, 11 seem to work okay.
Chapter 7 Instancing just shows a white screen instead of lots
of rotating shapes.
Chapter 8 SimpleVertexShader just shows a white screen instead of a rotating
red cube.
Chapter 14 TerraRendering seems ok, but the other programs in that chapter show either
a white or dark screen.

## Acknowledgement

This project has borrowed most of its code from
[kmscube](https://gitlab.freedesktop.org/mesa/kmscube)
