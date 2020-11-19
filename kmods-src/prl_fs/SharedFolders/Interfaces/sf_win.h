///////////////////////////////////////////////////////////////////////////////
///
/// @file sf_win.h
///
/// Constants and data structures for the SharedFolders Windows guest and host
/// components interaction
///
/// @author petrovv@
///
/// Copyright (c) 2008 Parallels, Inc.
/// All rights reserved.
/// http://www.parallels.com
///
///////////////////////////////////////////////////////////////////////////////

#ifndef __SF_WIN_H__
#define __SF_WIN_H__

#include <Build/CurrentProduct.h>

#define PRLFS_PROVIDER_NAME	"Parallels Shared Folders"


// Server name changed from ".psf" to "psf" in prl_fs.sys of version 2.0 (indicated in the TIS)
// Server name changed from "psf" to "Mac" in prl_fs.sys of version 4.0 (indicated in the TIS)
// Deprecated by introduction of dynamic server name selection in 4.4
#define PRLFS_SERVER_NAME	"Mac"


// The macro below lists all previously used names for PSF hostname along with
// prl_fs TIS version until a particular name has been used (at that version the
// name has been changed to another).
// The version is a combination of major/minor parts: ((major << 8) | minor).
// For example, .psf has been used until 0x0200, and since 0x0200 the name has been changed.
// To be more specific, at 0x0200 we've changed ".psf" to "psf",
// and at 0x0400 we've changed "psf" to "Mac"
#define PRLFS_COMPAT_SERVER_NAMES_(it) \
	it("psf", 0x0400) \
	it(".psf", 0x0200) \


// The positions of "Mac" and "psf" must be preserved because they are referenced
// by index in prl_fs.sys
//
// We have to use this #if to avoid leakage of any 'ChromeOS' bits with PDFM
// before public availability of PDFC
//
#if PRL_PROD(DESKTOP_CHROMEOS)
#define PRLFS_ALL_SERVER_NAMES_(it) \
	it("Mac") \
	it("ChromeOS") \
	PRLFS_COMPAT_SERVER_NAMES_(it) \

#else
#define PRLFS_ALL_SERVER_NAMES_(it) \
	it("Mac") \
	PRLFS_COMPAT_SERVER_NAMES_(it) \

#endif


// Still used in guest/host path convertion code. Essential for Linux guests. Please, do not use
// anywhere else. For compatibility purposes use the PRLFS_COMPAT_SERVER_NAMES_() macro instead.
#define PRLFS_SERVER_NAME_DOTPSF ".psf"


// prl_fs adds some formatted text into the TIS record in the format "key1:value1;key2:value2;"
// Below are tags for this format.
//
// Indicates currently used server name for \\<servername> (i.e., \\Mac)
#define PRLFS_TIS_TAG_PSFHOSTNAME "psfhost"


#endif
