///////////////////////////////////////////////////////////////////////////////
///
/// @file sf.h
///
/// Constants and data structures for the SharedFolders guest and host
/// components interaction
///
/// @author vasilyz@
/// @owner alexg@
///
/// Copyright (c) 2005-2007 Parallels Software International, Inc.
/// All rights reserved.
/// http://www.parallels.com
///
///////////////////////////////////////////////////////////////////////////////

#ifndef __SF_H__
#define __SF_H__

#include "Interfaces/ParallelsTypes.h"

#include <Interfaces/packed.h>

//
// There are generic rights
//
enum genericRights
{
	SF_GENERIC_READ		= 0x80000000L,
	//SF_GENERIC_WRITE	= 0x40000000L,
	//SF_GENERIC_EXECUTE	= 0x20000000L,
	//SF_GENERIC_ALL		= 0x10000000L,
};

////////////////////////////////////////////////////////////////////////////////
//
//			wdm.h
//
////////////////////////////////////////////////////////////////////////////////
enum accessTypes
{
	SF_SYNCHRONIZE		= 0x00100000L,
	SF_DELETE		= 0x00010000L,
};

////////////////////////////////////////////////////////////////////////////////
//
//			winnt.h
//
////////////////////////////////////////////////////////////////////////////////
enum shareOpts
{
	SF_FILE_SHARE_READ		= 0x00000001,
	SF_FILE_SHARE_WRITE		= 0x00000002,
	SF_FILE_SHARE_DELETE	= 0x00000004,
};

enum filePerm
{
	SF_FILE_READ_DATA			= 0x0001,		// file & pipe
	SF_FILE_LIST_DIRECTORY		= 0x0001,		// directory

	SF_FILE_WRITE_DATA			= 0x0002,		// file & pipe
	SF_FILE_ADD_FILE			= 0x0002,		// directory

	SF_FILE_APPEND_DATA			= 0x0004,		// file
	SF_FILE_ADD_SUBDIRECTORY	= 0x0004,		// directory
	SF_FILE_CREATE_PIPE_INSTANCE= 0x0004,		// named pipe

	SF_FILE_READ_EA				= 0x0008,		// file & directory

	SF_FILE_WRITE_EA			= 0x0010,		// file & directory

	SF_FILE_EXECUTE				= 0x0020,		// file
	SF_FILE_TRAVERSE			= 0x0020,		// directory

	SF_FILE_DELETE_CHILD		= 0x0040,		// directory

	SF_FILE_READ_ATTRIBUTES		= 0x0080,		// all

	SF_FILE_WRITE_ATTRIBUTES	= 0x0100,		// all
};

enum fileAttrs
{
	SF_FILE_ATTRIBUTE_READONLY		= 0x00000001,
	SF_FILE_ATTRIBUTE_HIDDEN		= 0x00000002,
	SF_FILE_ATTRIBUTE_SYSTEM		= 0x00000004,
	SF_FILE_ATTRIBUTE_DIRECTORY		= 0x00000010,
	//#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
	//#define FILE_ATTRIBUTE_DEVICE               0x00000040
	SF_FILE_ATTRIBUTE_NORMAL		= 0x00000080,
	SF_FILE_ATTRIBUTE_TEMPORARY		= 0x00000100,
	//#define FILE_ATTRIBUTE_SPARSE_FILE          0x00000200
	SF_FILE_ATTRIBUTE_REPARSE_POINT	= 0x00000400,
	//#define FILE_ATTRIBUTE_COMPRESSED           0x00000800
	SF_FILE_ATTRIBUTE_OFFLINE = 0x00001000,
	//#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  0x00002000
	//#define FILE_ATTRIBUTE_ENCRYPTED            0x00004000
	//#define FILE_ATTRIBUTE_VIRTUAL              0x00010000
};


////////////////////////////////////////////////////////////////////////////////
//
//			ntifs.h
//
////////////////////////////////////////////////////////////////////////////////
enum openFlags
{
	SF_FILE_DIRECTORY_FILE		= 0x00000001,
	SF_FILE_NON_DIRECTORY_FILE	= 0x00000040,
};

enum createOptions
{
	//
	// create/open option flags
	//
	SF_FILE_DELETE_ON_CLOSE		= 0x00001000,
};

enum dispositions
{
	//
	// create disposition values
	//
	SF_FILE_SUPERSEDE			= 0x00000000,
	SF_FILE_OPEN				= 0x00000001,
	SF_FILE_CREATE				= 0x00000002,
	SF_FILE_OPEN_IF				= 0x00000003,
	SF_FILE_OVERWRITE			= 0x00000004,
	SF_FILE_OVERWRITE_IF		= 0x00000005,
	//FILE_MAXIMUM_DISPOSITION	= 0x00000005,


	//
	// I/O status information return values for NtCreateFile/NtOpenFile
	//
	SF_FILE_SUPERSEDED			= 0x00000000,
	SF_FILE_OPENED				= 0x00000001,
	SF_FILE_CREATED				= 0x00000002,
	SF_FILE_OVERWRITTEN			= 0x00000003,
	//SF_FILE_EXISTS				= 0x00000004,
	//SF_FILE_DOES_NOT_EXIST		= 0x00000005,
};


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


// Parameters for TG_REQUEST_FS_GETVERSION
enum sfProtoVersions
{
	SF_PROTO_V1 = 1,	// never reported, assumed if TG_REQUEST_FS_GETVERSION fails (i.e. not implemented)
	SF_PROTO_V2 = 2,	// support for delayed close indroduced
	SF_PROTO_V3 = 3,	// support for directory change notifications introduced
	SF_PROTO_V4 = 4,	// TG_REQUEST_FS_CONTROL indroduced
	SF_PROTO_V5 = 5,	// fileIds in createFile responses
	SF_PROTO_V6 = 6,	// support close-on-eof for directory enumerations

	__SF_PROTO_COMPUTE_LATEST,
	SF_PROTO_LATEST = __SF_PROTO_COMPUTE_LATEST - 1
};


enum sfProtoFlags
{
	SF_FLAG_DIRNOTIF_ENABLED = 0x0001,
};


// Must fit in 32bit
typedef struct _GETVERSION_RESULT_BLOCK
{
	UINT16 version;			// a constant from sfProtoVersions enum
	UINT16 flags;			// see the sfProtoFlags enum
} GETVERSION_RESULT_BLOCK;


// Parameters for TG_REQUEST_FS_CONTROL
enum sfControlRequests
{
	// 0 is reserved
	SFCTL_GETCACHELEVEL = 1,
	SFCTL_GETDIRNOTIFYDISABLE = 2,
	SFCTL_GETFILEIDENABLE = 3,
	SFCTL_MAX_SUPPORTED
};


typedef struct _FS_CONTROL_PARAMS_v0
{
	// Filled by Guest
	UINT32 size;
	UINT32 ControlCode;

	// Filled by host in response

	UINT32 ControlMax;				// Host puts here max supported control code
	UINT32 SizeMax;

	union {
		UINT32 preallocated[32];
		UINT32 CacheLevel;
		UINT32 DirNotifyDisable;
		UINT32 FileIDsEnable;
	};

} FS_CONTROL_PARAMS_v0;

typedef FS_CONTROL_PARAMS_v0 FS_CONTROL_PARAMS;


////////////////////////////////////////////////////////////////////////////////
// parameters for SHFOLD_CREATEFILE function
typedef struct _CREATE_PARAMETERS_BLOCK
{
	ULONG32			NrHandle;
	ULONG32			DesiredAccess;
	LONG64			AllocationSize;
	ULONG32			FileAttributes;
	ULONG32			ShareAccess;
	ULONG32			Disposition;
	ULONG32			CreateOptions;
	ULONG32			FnLength;
	UINT16			FileName[1];
} CREATE_PARAMETERS_BLOCK;
typedef CREATE_PARAMETERS_BLOCK *PCREATE_PARAMETERS_BLOCK;


// results returned by SHFOLD_CREATEFILE function
typedef struct _CREATE_RESULT_BLOCK
{
	ULONG32 bCaseSensitive;
	ULONG32 FileHandle;
	ULONG32 CreateAction;
	LONG64 CreationTime;
	LONG64 LastAccessTime;
	LONG64 LastWriteTime;
	LONG64 ChangeTime;
	LONG64 EndOfFile;
	LONG64 AllocationSize;
	ULONG32 FileAttributes;
	ULONG32 bDeleteOnClose;
} CREATE_RESULT_BLOCK;
typedef CREATE_RESULT_BLOCK *PCREATE_RESULT_BLOCK;

typedef struct {
	char test[(sizeof(CREATE_RESULT_BLOCK) == 68) * 2 - 1];
} assert_packing;


////////////////////////////////////////////////////////////////////////////////
// parameters for SHFOLD_RENAMEFILE function
typedef struct _RENAME_PARAMETERS_BLOCK
{
	ULONG32			FileHandle;
	ULONG32			bReplaceIfExists;
	ULONG32			FnLength;
	UINT16			FileName[1];
} RENAME_PARAMETERS_BLOCK;

typedef RENAME_PARAMETERS_BLOCK *PRENAME_PARAMETERS_BLOCK;


////////////////////////////////////////////////////////////////////////////////
// parameters for SHFOLD_CLOSEHANDLE function
typedef struct _CLOSE_PARAMETERS_BLOCK
{
	ULONG32 FileHandle;
} CLOSE_PARAMETERS_BLOCK;

typedef CLOSE_PARAMETERS_BLOCK *PCLOSE_PARAMETERS_BLOCK;

#define SHFOLD_CLOSEHANDLES_MAX 3
#define LAZY_CLOSE_TIMEOUT (500 * 10000) // 0.5 sec

////////////////////////////////////////////////////////////////////////////////
// parameters for TG_REQUEST_FS_CLOSEHANDLES function
typedef struct _CLOSEX_PARAMETERS_BLOCK
{
	ULONG32 FileHandleCount;
	ULONG32 FileHandle[SHFOLD_CLOSEHANDLES_MAX];
} CLOSEX_PARAMETERS_BLOCK, *PCLOSEX_PARAMETERS_BLOCK;

// The following struct lays over CLOSEX_PARAMETERS_BLOCK in the TG request
// layout. It may be valid or not valid upon return from the operation. The
// validness depends on the host version. Since DelayedClose.FileHandleCount
// may use only 2 lowest bits, then another 30 bits are always 0 on old
// hosts and we are free to use any of them to indicate validness and then
// reuse all another bits that cover the whole CLOSEX_PARAMETERS_BLOCK.
typedef struct _CREATE_RESULT_BLOCK_ADD
{
	unsigned reservedBits31:31;
	unsigned valid:1;
	LONG64 fileId;
	ULONG32 reserved;
} CREATE_RESULT_BLOCK_ADD;
typedef CREATE_RESULT_BLOCK_ADD *PCREATE_RESULT_BLOCK_ADD;

////////////////////////////////////////////////////////////////////////////////
// parameters for TG_REQUEST_FS_CREATEFILEX function
typedef struct _CREATEFILEX_PARAMETERS_BLOCK
{
	union
	{
		CLOSEX_PARAMETERS_BLOCK DelayedClose;
		CREATE_RESULT_BLOCK_ADD CreateResultAdd;
	};
	CREATE_RESULT_BLOCK CreateResult;
} CREATEFILEX_PARAMETERS_BLOCK, *PCREATEFILEX_PARAMETERS_BLOCK;

////////////////////////////////////////////////////////////////////////////////
// parameters for SHFOLD_SETDISPOSITION function
typedef struct _DISPOSITION_PARAMETERS_BLOCK
{
	ULONG32 FileHandle;
	ULONG32 bDeleteOnClose;
} DISPOSITION_PARAMETERS_BLOCK;

typedef DISPOSITION_PARAMETERS_BLOCK *PDISPOSITION_PARAMETERS_BLOCK;


////////////////////////////////////////////////////////////////////////////////
// parameters for SHFOLD_QUERYDYRINIT function
enum queryDirFeatures
{
	SF_QD_SHORT_NAMES = 0x1,
	SF_QD_PACK_GETDATA_RESULT = 0x2,
	SF_QD_CLOSE_ON_EOF = 0x4,			// SF_QD_PACK_GETDATA_RESULT must also be set
};


typedef struct _QUERY_DIR_PARAMETERS_EX
{
	ULONG32 Size;				// must be set to sizeof(QUERY_DIR_PARAMETERS_EX)
	ULONG32 RequestedFeatures;	// flags, see queryDirFeatures enum
	ULONG32 reserved[2];		// must be zeroed
} QUERY_DIR_PARAMETERS_EX;


typedef struct _QUERY_DIR_PARAMETERS_BLOCK
{
	ULONG32			FileHandle;
	ULONG32			FilterLength;
	UINT16			Filter[1];
	// QUERY_DIR_PARAMETERS_EX may follow at offset (sizeof(QUERY_DIR_PARAMETERS_BLOCK) + FilterLength)
	// If the EX struct is supplied, the caller must be able to accept QUERY_DIR_RESULT_EX
} QUERY_DIR_PARAMETERS_BLOCK;

typedef QUERY_DIR_PARAMETERS_BLOCK *PQUERY_DIR_PARAMETERS_BLOCK;


typedef struct _QUERY_DIR_RESULT
{
	ULONG32 SearchHandle;
} QUERY_DIR_RESULT;


enum queryDirResultFlags
{
	// Reached end of the directory enumeration. If CLOSE_ON_EOF was requested, then the enum has been closed.
	SF_QDR_EOF = 0x1,
};


// Host must ensure guest support before sending the struct
typedef struct _QUERY_DIR_RESULT_EX
{
	union
	{
		ULONG32 ExIndicator;		// belongs to SearchHandle in old version; if zero, the response is of EX version
		ULONG32 SearchHandleOld;
	};
	ULONG32 Size;				// set to sizeof(QUERY_DIR_RESULT_EX)
	ULONG32 SearchHandle;
	ULONG32 EnabledFeatures;	// flags, see queryDirFeatures enum
	ULONG32 ValidMask;			// indicate meaningful flags in the ResultFlags field
	ULONG32 ResultFlags;
} QUERY_DIR_RESULT_EX, QUERY_DIR_RESULT_EX_v0;


typedef struct _QUERY_DIR_GETDATA_RESULT
{
	ULONG32 Size;				// set to sizeof(QUERY_DIR_GETDATA_RESULT)
	ULONG32 ValidMask;			// indicate meaningful flags in the ResultFlags field
	ULONG32 ResultFlags;
} QUERY_DIR_GETDATA_RESULT, QUERY_DIR_GETDATA_RESULT_v0;


typedef struct _QUERY_DIR_CLOSE_PARAMS
{
	ULONG32 SearchHandle;
	ULONG32 FileHandle;
	ULONG32 DirNotifyKey;
} QUERY_DIR_CLOSE_PARAMS;


enum notifyChangeDirFlags
{
	SF_NCD_WATCH_TREE = 0x1,
	SF_NCD_HAS_KEY = 0x2,
};


// the constants below must have the same values as appropriate
// FILE_NOTIFY_CHANGE_XXX constants in Windows SDK
enum notifyCompletionFlags
{
	SFF_FILE_NOTIFY_CHANGE_FILE_NAME = 0x00000001,
	SFF_FILE_NOTIFY_CHANGE_DIR_NAME = 0x00000002,
	SFF_FILE_NOTIFY_CHANGE_ATTRIBUTES = 0x00000004,
	SFF_FILE_NOTIFY_CHANGE_SIZE = 0x00000008,
	SFF_FILE_NOTIFY_CHANGE_LAST_WRITE = 0x00000010,
	SFF_FILE_NOTIFY_CHANGE_LAST_ACCESS = 0x00000020,
	SFF_FILE_NOTIFY_CHANGE_CREATION = 0x00000040,
	SFF_FILE_NOTIFY_CHANGE_SECURITY = 0x00000100,
};


typedef struct _NOTIFYCHANGEDIR_PARAMETERS_BLOCK
{
	ULONG32 FileHandle;
	ULONG32 CompletionFilter;
	ULONG32 Flags;
	ULONG32 DirNotifyKey;
	ULONG32 reserved[2];
} NOTIFYCHANGEDIR_PARAMETERS_BLOCK;


// the constants below must have the same values as appropriate
// FILE_ACTION_XXX constants in Windows SDK
enum fsFileActions
{
	SFF_FILE_ACTION_ADDED = 0x00000001,
	SFF_FILE_ACTION_REMOVED = 0x00000002,
	SFF_FILE_ACTION_MODIFIED = 0x00000003,
	SFF_FILE_ACTION_RENAMED_OLD_NAME = 0x00000004,
	SFF_FILE_ACTION_RENAMED_NEW_NAME = 0x00000005,
};


// the structure must be defined as FILE_NOTIFY_INFORMATION in Windows SDK
typedef struct _SF_FILE_NOTIFY_INFORMATION
{
	ULONG32 NextEntryOffset;
	ULONG32 Action;
	ULONG32 FileNameLength;
	UINT16 FileName[1];
} SF_FILE_NOTIFY_INFORMATION;


typedef struct _FS_DIRECTORY_INFORMATION
{
	LONG64 CreationTime;
	LONG64 LastAccessTime;
	LONG64 LastWriteTime;
	LONG64 ChangeTime;
	LONG64 EndOfFile;
	LONG64 AllocationSize;
	ULONG32 FileAttributes;
	ULONG32 FileNameLength;
	UINT16 FileName[1];
} FS_DIRECTORY_INFORMATION;

typedef FS_DIRECTORY_INFORMATION *PFS_DIRECTORY_INFORMATION;


// Host must ensure guest support before sending the struct
typedef struct _FS_DIRECTORY_INFORMATION_EX
{
	LONG64 CreationTime;
	LONG64 LastAccessTime;
	LONG64 LastWriteTime;
	LONG64 ChangeTime;
	LONG64 EndOfFile;
	LONG64 AllocationSize;
	ULONG32 FileAttributes;
	// If zero, the structure is of EX version, and if nonzero, the structure is of old version
	ULONG32 ExIndicator;				// belongs to FileNameLength of old struct; 0 will mean extended struct
	ULONG32 Size;						// must be set to sizeof(_FS_DIRECTORY_INFORMATION_EX)
	UINT16 ShortNameLength;				// host provides shortname for the file; if not provided, guest may
	UINT16 ShortName[12];				// generate its own version of shortname for the file
	ULONG32 FileNameLength;
	ULONG32 reserved[2];				// must be zeroed
	// place new fields here

	// Must always be at the end;
	// When parse, compute offset of the field manually, using Size member
	UINT16 FileName[1];
} FS_DIRECTORY_INFORMATION_EX;



////////////////////////////////////////////////////////////////////////////////
typedef struct _FS_RW_DATA_PARAMETERS_BLOCK {
	ULONG32		FileHandle;
	LONG64		Offset;
	ULONG32		Length;
} FS_RW_DATA_PARAMETERS_BLOCK;
typedef FS_RW_DATA_PARAMETERS_BLOCK *PFS_RW_DATA_PARAMETERS_BLOCK;


////////////////////////////////////////////////////////////////////////////////
typedef struct _BASIC_PARAMETERS_BLOCK {
	ULONG32		FileHandle;
	LONG64		CreationTime;
	LONG64		LastAccessTime;
	LONG64		LastWriteTime;
	LONG64		ChangeTime;
	ULONG32		FileAttributes;
} BASIC_PARAMETERS_BLOCK;
typedef BASIC_PARAMETERS_BLOCK *PBASIC_PARAMETERS_BLOCK;


////////////////////////////////////////////////////////////////////////////////
typedef struct _ALLOC_PARAMETERS_BLOCK
{
	ULONG32		FileHandle;
	LONG64		AllocationSize;
} ALLOC_PARAMETERS_BLOCK;
typedef ALLOC_PARAMETERS_BLOCK *PALLOC_PARAMETERS_BLOCK;



typedef struct _SIZE_PARAMETERS_BLOCK {
	ULONG32			FileHandle;
	LONG64	FileSize;
} SIZE_PARAMETERS_BLOCK;
typedef SIZE_PARAMETERS_BLOCK *PSIZE_PARAMETERS_BLOCK;

typedef struct _FS_SIZE_INFO_BLOCK {
	//FILE_FS_SIZE_INFORMATION info;
    LONG64		TotalAllocationUnits;
    LONG64		AvailableAllocationUnits;
    ULONG32		SectorsPerAllocationUnit;
    ULONG32		BytesPerSector;
} FS_SIZE_INFO_BLOCK;
typedef FS_SIZE_INFO_BLOCK *PFS_SIZE_INFO_BLOCK;

enum psfHostFlags
{
	PSF_FLAG_IS_AUTOMOUNT = 0,
};

typedef struct _TG_PSF_GET_HOST_FLAGS_HEADER {
	UINT32		Flags;
} TG_PSF_GET_HOST_FLAGS_HEADER;

#include <Interfaces/unpacked.h>

#endif
