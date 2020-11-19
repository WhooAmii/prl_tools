///////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) 2013 Parallels Software International, Inc.
/// All rights reserved.
/// http://www.parallels.com
///
///////////////////////////////////////////////////////////////////////////////

#pragma once


// Persistent application ID for special shared folders.
typedef enum _PSF_FOLDER_APP_ID
{
	PSF_FOLDER_APP_STANDARD = 0,
	PSF_FOLDER_APP_ICLOUD = 1,
	PSF_FOLDER_APP_PHOTOSTREAM = 2,
	PSF_FOLDER_APP_DROPBOX = 3,
	PSF_FOLDER_APP_GOOGLEDRIVE = 4,
	PSF_FOLDER_APP_ICLOUDDRIVE = 5,
	PSF_FOLDER_APP_PHOTOLIBRARY = 6,
	PSF_FOLDER_APP_DROPBOX_BUSINESS = 7,
	PSF_FOLDER_APP_BOX = 8,
} PSF_FOLDER_APP_ID;


// TG_REQUEST_PSF_GETLIST
typedef enum _PSF_FOLDER_FLAGS
{
	PSF_FOLDER_READONLY = 1 << 0,
	PSF_FOLDER_GLOBAL = 1 << 1,
	PSF_FOLDER_HOME = 1 << 2,
	PSF_FOLDER_USERDEFINED = 1 << 3,
	PSF_FOLDER_SMARTMOUNT = 1 << 4,
	PSF_FOLDER_CLOUD = 1 << 5,
	PSF_FOLDER_SUIDENABLED = 1 << 6,
} PSF_FOLDER_FLAGS;


typedef struct _TGR_PSF_GETLIST_INLINE
{
	UINT32 filter;
} TGR_PSF_GETLIST_INLINE;


typedef struct _PSF_ITEM
{
	UINT32 size;
	UINT32 flags;			// PSF_FOLDER_FLAGS
	UINT16 appId;			// PSF_FOLDER_APP_ID
	UINT8 _reserved[2 + 3 * 4];
	UINT8 name[1];
} PSF_ITEM;

