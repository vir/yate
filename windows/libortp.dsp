# Microsoft Developer Studio Project File - Name="libortp" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=libortp - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libortp.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libortp.mak" CFG="libortp - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libortp - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "libortp - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libortp - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "libortp___Win32_Release"
# PROP BASE Intermediate_Dir "libortp___Win32_Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /W3 /O2 /I "." /I ".." /I "..\contrib\ortp" /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /FD /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "libortp - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "libortp___Win32_Debug"
# PROP BASE Intermediate_Dir "libortp___Win32_Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /Zi /Od /I "." /I ".." /I "..\contrib\ortp" /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /FD /GZ /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "libortp - Win32 Release"
# Name "libortp - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\contrib\ortp\avprofile.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\export.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\ortp.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\payloadtype.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\port_fct.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\posixtimer.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpmod.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpparse.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpsession.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpsignaltable.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtptimer.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\scheduler.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\sessionset.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\str_utils.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\telephonyevents.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE="..\contrib\ortp\errno-win32.h"
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\export.h
# End Source File
# Begin Source File

SOURCE="..\contrib\ortp\ortp-config-win32.h"
# End Source File
# Begin Source File

SOURCE="..\contrib\ortp\ortp-config.h"
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\ortp.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\payloadtype.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\port_fct.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtcp.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtp.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpmod.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpport.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpsession.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtpsignaltable.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\rtptimer.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\scheduler.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\sessionset.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\str_utils.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ortp\telephonyevents.h
# End Source File
# End Group
# End Target
# End Project
