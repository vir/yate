/**
 * MacOSXUtils.mm
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */


#include <MacOSXUtils.h>

#import <Foundation/Foundation.h>

using namespace TelEngine;

// utility function to get const char* from NSString*
static const char* fromNSString(NSString* str)
{
    if (!str)
	return 0;
    return [str cStringUsingEncoding:NSUTF8StringEncoding];
}

// utility function to get a NSString from a const char*
static NSString* toNSString(const char* chars)
{
    if (!chars)
	return 0;
    return [NSString stringWithCString:chars encoding:NSUTF8StringEncoding];
}

// extend the NSFileManager class
@interface NSFileManager (MacOSXUtils)

/**
  * Get path of the given directory type
  * @param directory Type of directory to search for
  * @param domainMask Mask specifying domains in which to search for directory
  * @param create True to create directory if it doesn't exist
  * @param pathComponent What to append to the directory path
  * @return The path to the searched directory
  */
-(NSString*)pathForDirectory:(NSSearchPathDirectory)directory
		    inDomain:(NSSearchPathDomainMask)domainMask
		   createDir:(bool)create
		 byAppending:(NSString*)pathComponent;

/**
  * Get the path to the ~/Library/Application Support directory by appending to it the given appName. Create if it doesn't exist
  */
-(NSString*)applicationSupportDirectoryForApp:(NSString*)appName;

@end


@implementation NSFileManager (MacOSXUtils)

-(NSString*)pathForDirectory:(NSSearchPathDirectory)directory
		    inDomain:(NSSearchPathDomainMask)domainMask
		   createDir:(bool)create
		 byAppending:(NSString*)pathComponent
{
    NSArray* paths = NSSearchPathForDirectoriesInDomains(directory,domainMask,YES);
    if ([paths count] == 0) {
	Debug(DebugInfo,"Failed to get path for directory='%u' in domains='%u'",(unsigned int)directory,(unsigned int)domainMask);
	return nil;
    }

    NSString* path = [paths objectAtIndex:0];

    if (pathComponent && [pathComponent length])
	path = [path stringByAppendingPathComponent:pathComponent];

    NSError *error = nil;
    if (!([self createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:&error])) {
	Debug(DebugInfo,"Failed to create path '%s' error code = %d",fromNSString(path),(int)[error code]);
	return nil;
    }

    return path;
}

-(NSString*)applicationSupportDirectoryForApp:(NSString*)appName
{
    NSString *result = [self pathForDirectory:NSApplicationSupportDirectory
				     inDomain:NSUserDomainMask
				    createDir:YES
				  byAppending:appName];
    return result;
}

@end


static NSSearchPathDirectory translateDirectory(MacOSXUtils::DirectoryPath dirPath)
{
    switch (dirPath) {
	case MacOSXUtils::ApplicationDirectory:
	    return NSApplicationDirectory;
	case MacOSXUtils::DemoApplicationDirectory:
	    return NSDemoApplicationDirectory;
	case MacOSXUtils::DeveloperApplicationDirectory:
	    return NSDeveloperApplicationDirectory;
	case MacOSXUtils::AdminApplicationDirectory:
	    return NSAdminApplicationDirectory;
	case MacOSXUtils::LibraryDirectory:
	    return NSLibraryDirectory;
	case MacOSXUtils::DeveloperDirectory:
	    return NSDeveloperDirectory;
	case MacOSXUtils::UserDirectory:
	    return NSUserDirectory;
	case MacOSXUtils::DocumentationDirectory:
	    return NSDocumentationDirectory;
#ifdef MAC_OS_X_VERSION_10_6
	case MacOSXUtils::DocumentDirectory:
	    return NSDocumentDirectory;
#endif
	case MacOSXUtils::CoreServiceDirectory:
	    return NSCoreServiceDirectory;
#ifdef MAC_OS_X_VERSION_10_6
	case MacOSXUtils::AutosavedInformationDirectory:
	    return NSAutosavedInformationDirectory;
#endif
	case MacOSXUtils::DesktopDirectory:
	    return NSDesktopDirectory;
	case MacOSXUtils::CachesDirectory:
	    return NSCachesDirectory;
	case MacOSXUtils::ApplicationSupportDirectory:
	    return NSApplicationSupportDirectory;
	case MacOSXUtils::DownloadsDirectory:
	    return NSDownloadsDirectory;
#ifdef MAC_OS_X_VERSION_10_6
	case MacOSXUtils::InputMethodsDirectory:
	    return NSInputMethodsDirectory;
	case MacOSXUtils::MoviesDirectory:
	    return NSMoviesDirectory;
	case MacOSXUtils::MusicDirectory:
	    return NSMusicDirectory;
	case MacOSXUtils::PicturesDirectory:
	    return NSPicturesDirectory;
	case MacOSXUtils::PrinterDescriptionDirectory:
	    return NSPrinterDescriptionDirectory;
	case MacOSXUtils::SharedPublicDirectory:
	    return NSSharedPublicDirectory;
	case MacOSXUtils::PreferencePanesDirectory:
	    return NSPreferencePanesDirectory;
	case MacOSXUtils::ItemReplacementDirectory:
	    return NSItemReplacementDirectory;
#endif
	case MacOSXUtils::AllApplicationsDirectory:
	    return NSAllApplicationsDirectory;
	case MacOSXUtils::AllLibrariesDirectory:
	    return NSAllLibrariesDirectory;
	default:
	    return 0;
    }
    return 0;
}

static NSSearchPathDomainMask translateDomain(unsigned int domainMask)
{
    NSSearchPathDomainMask mask = 0;
    if (domainMask & MacOSXUtils::UserDomainMask)
	mask |= NSUserDomainMask;
    if (domainMask & MacOSXUtils::LocalDomainMask)
	mask |= NSLocalDomainMask;
    if (domainMask & MacOSXUtils::NetworkDomainMask)
	mask |= NSNetworkDomainMask;
    if (domainMask & MacOSXUtils::SystemDomainMask)
	mask |= NSSystemDomainMask;
    if (domainMask & MacOSXUtils::AllDomainsMask)
	mask |= NSAllDomainsMask;

    return mask;
}

void MacOSXUtils::applicationSupportPath(String& path, const char* appName)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSString* app = toNSString(appName);

    NSString* pth = [[NSFileManager defaultManager] applicationSupportDirectoryForApp:app];
    path = fromNSString(pth);
    [pool release];
}

void MacOSXUtils::getPath(String& path, DirectoryPath dirPath, Domain domain, const char* append, bool createDir)
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSSearchPathDirectory dir = translateDirectory(dirPath);
    NSSearchPathDomainMask domainMask = translateDomain(domain);

    NSString* pth = [[NSFileManager defaultManager] pathForDirectory:dir inDomain:domainMask createDir:createDir byAppending:toNSString(append)];
    path = fromNSString(pth);
    [pool release];
}
