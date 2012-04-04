/*
 * MacOSXUtils.h
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Classes for interfacing with Mac OS X API
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2012 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifndef __MACOSXUTILS_H
#define __MACOSXUTILS_H

#include <yatecbase.h>

namespace  TelEngine {

class YATE_API MacOSXUtils
{
public:
    /**
     * Directory search paths
     */
    enum DirectoryPath {
	ApplicationDirectory = 1,
	DemoApplicationDirectory,
	DeveloperApplicationDirectory,
	AdminApplicationDirectory,
	LibraryDirectory,
	DeveloperDirectory,
	UserDirectory,
	DocumentationDirectory,
	DocumentDirectory,
	CoreServiceDirectory,
	AutosavedInformationDirectory,
	DesktopDirectory,
	CachesDirectory,
	ApplicationSupportDirectory,
	DownloadsDirectory,
	InputMethodsDirectory,
	MoviesDirectory,
	MusicDirectory,
	PicturesDirectory,
	PrinterDescriptionDirectory,
	SharedPublicDirectory,
	PreferencePanesDirectory,
	ItemReplacementDirectory,
	AllApplicationsDirectory,
	AllLibrariesDirectory,
    };

    /**
     * Domains for searching directory paths
     */
    enum Domain {
	UserDomainMask = 1,
	LocalDomainMask = 2,
	NetworkDomainMask = 4,
	SystemDomainMask = 8,
	AllDomainsMask = 0x0ffff,
    };

    /**
     * Get path of the Application Support directory for the user
     * @param path String into which the determined path will go
     * @param appName Name of directory to create in the Application Support
     */
    static void applicationSupportPath(String& path, const char* appName = 0);

    /**
     * Get path of a given type of directory in a certain domain
     * @param path String into which the determined path will go
     * @param dirPath The type of directory path to be sought
     * @param domain Mask containing the domains where the type of directory is to be sought
     * @param append Name of directory to create in the searched directory 
     * @param append True if the directory is to be created if it not exists
     */
    static void getPath(String& path, DirectoryPath dirPath, Domain domain, const char* append = 0, bool createDir = true);
};

}; // namespece TelEngine

#endif /* __MACOSXUTILS_H */

/* vi: set ts=8 sw=4 sts=4 noet: */
