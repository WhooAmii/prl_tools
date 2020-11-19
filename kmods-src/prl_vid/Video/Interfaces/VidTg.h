//////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2007 Parallels Inc. All Rights Reserved.
/// http://www.parallels.com
///
/// PCI toolgate structures and constants that walk
/// from guest kernel mode to host application
///
/// note that this interface must be common for
/// 32 and 64 bit architectures
///
/// @author sergeyv@
///
//////////////////////////////////////////////////////////////////////////
#ifndef __VID_TG_H__
#define __VID_TG_H__

//
//#include <Tools/Toolgate/Interfaces/Tg.h>

// Capabilities flags
#define PRLVID_CAPABILITY_ACCELERATED		(1 << 0)
#define PRLVID_CAPABILITY_APERTURE_ONLY		(1 << 1)

//
// TG_SET_MOUSE_POINTER inline data
// 32-bit BGRA pointer bitmap comes in buffer[0]
typedef struct _VID_TG_SET_MOUSE_POINTER {
	int x;
	int y;
	int hotx;
	int hoty;
	int width;
	int height;
	int stride;
} VID_TG_SET_MOUSE_POINTER;


//
// Multi-head support
typedef struct _VID_TG_QUERY_HEADS {
	unsigned heads;
	unsigned connected;
} VID_TG_QUERY_HEADS;

typedef struct _VID_TG_ENABLE_HEAD {
	unsigned head;
	unsigned reserved;
} VID_TG_ENABLE_HEAD;

typedef struct _VID_TG_MODE {
	unsigned short head;
	unsigned short bpp;
	unsigned short width;
	unsigned short height;
	unsigned stride;
	unsigned refresh;
	unsigned short flags;
#define VID_TG_MODE_CLEAR 1
#define VID_TG_MODE_HIDPI 2
	unsigned short dpi;
	unsigned offset32;
	// display location
	int x;
	int y;
} VID_TG_MODE;

typedef struct _VID_TG_MODE64 {
	unsigned short head;
	unsigned short bpp;
	unsigned short width;
	unsigned short height;
	unsigned stride;
	unsigned refresh;
	unsigned short flags;
	unsigned short dpi;
	unsigned offset32;
	// display location
	int x;
	int y;
#ifdef _MSC_VER
	unsigned __int64 offset64;
#else
	unsigned long long offset64;
#endif
} VID_TG_MODE64;

typedef struct _VID_TG_OFFSET {
	unsigned short head;
	unsigned short reserved;
	unsigned offset;
} VID_TG_OFFSET;

// 32-bit RGBX colors come in buffer[0]
typedef struct _VID_TG_PALETTE {
	unsigned short head;
	unsigned short reserved;
	unsigned short index;
	unsigned short count;
} VID_TG_PALETTE;

// guest-to-host shared area: per-head framebuffer updates
// should be accessed via appropriate atimic operations
typedef union _VID_TG_UPDATE_BOUNDS {
	struct {
		short left;
		short top;
		short right;
		short bottom;
	} s;
#ifdef _MSC_VER
	unsigned __int64 u;
#else
	unsigned long long u;
#endif
} VID_TG_UPDATE_BOUNDS;

// guest-to-host shared area: desktop-space mouse position
// should be accessed via appropriate atimic operations
typedef union _VID_TG_MOUSE_POSITION {
	struct {
		int x;
		int y;
	} s;
#ifdef _MSC_VER
	unsigned __int64 u;
#else
	unsigned long long u;
#endif
} VID_TG_MOUSE_POSITION;

typedef struct _VID_TG_MAP_APERTURE {
#ifdef _MSC_VER
	unsigned __int64 VidMemAddress;
#else
	unsigned long long VidMemAddress;
#endif
} VID_TG_MAP_APERTURE;

typedef struct _VID_TG_UNMAP_APERTURE {
#ifdef _MSC_VER
	unsigned __int64 VidMemAddress;
#else
	unsigned long long VidMemAddress;
#endif
} VID_TG_UNMAP_APERTURE;

typedef struct _VID_TG_GAMMA_RAMP {
	unsigned head;
	unsigned type;
} VID_TG_GAMMA_RAMP;

//
// generic rectangle in screen coordinates
typedef struct _VID_TG_RECT {
	int left;
	int top;
	int right;
	int bottom;
} VID_TG_RECT;

//
// OpenGL
typedef struct _VID_TG_GL_VERSION {
	unsigned short major;
#define VID_TG_GL_VERSION_MAJOR 3
	unsigned short minor;
#define VID_TG_GL_VERSION_MINOR 0
} VID_TG_GL_VERSION;

// context and buffer creation parameters
typedef struct _VID_TG_GL_CREATE {
	unsigned process;
	unsigned handle;
	// format is described by a sequence of GL constants
	// and (possibly) values terminated by GL_NONE
	// for now we only care about GL_DOUBLEBUFFER
	// TO BE SPECIFIED
	unsigned short format[60];
} VID_TG_GL_CREATE;

// context and buffer destruction parameters
typedef struct _VID_TG_GL_DESTROY {
	unsigned process;
	unsigned handle;
} VID_TG_GL_DESTROY;

// context copy or sharing
typedef struct _VID_TG_GL_COPY {
	unsigned process;
	unsigned src;
	unsigned dst;
	unsigned mask;
} VID_TG_GL_COPY;

typedef struct _VID_TG_GL_COMMAND {
	unsigned process;
	unsigned context;
	unsigned buffer;
	// GL_NONE/GL_FRONT/GL_BACK
	unsigned draw;
	VID_TG_RECT bounds;
} VID_TG_GL_COMMAND;

// extended syntax used:
// - for WDDM to blit GL buffer to DX surface
// - for Linux DRM to draw GL buffer to the desired head
typedef struct _VID_TG_GL_COMMAND2 {
	VID_TG_GL_COMMAND command;
	// DX resource to blit GL buffer to
#ifdef _MSC_VER
	unsigned __int64 resource;
#else
	unsigned long long resource;
#endif
	// head to draw GL buffer to
	unsigned head;
	unsigned reserved;
	VID_TG_RECT rect;
} VID_TG_GL_COMMAND2;

typedef struct _VID_TG_DIRECT3D_GET_RESOURCE_INFO {
	unsigned int ResourceType;
	unsigned int Format;
#ifdef _MSC_VER
	unsigned __int64 Width;
#else
	unsigned long long Width;
#endif
	unsigned int Height;
	unsigned short DepthOrArraySize;
	unsigned short MipLevels;
	unsigned int Layout;
	unsigned int SampleCount;
	unsigned int SampleQuality;
	unsigned int Flags;
} VID_TG_DIRECT3D_GET_RESOURCE_INFO;

typedef struct _VID_TG_DIRECT3D_RESOURCE_INFO {
#ifdef _MSC_VER
	unsigned __int64 Size;
#else
	unsigned long long Size;
#endif
	unsigned int Alignment;
	unsigned int Reserved;
} VID_TG_DIRECT3D_RESOURCE_INFO;

#endif // __VID_TG_H__
