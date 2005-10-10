# Microsoft Developer Studio Project File - Name="libilbc" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=libilbc - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libilbc.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libilbc.mak" CFG="libilbc - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libilbc - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "libilbc - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libilbc - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release\ilbc"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MD /W3 /O2 /I "." /I ".." /I "..\contrib\ilbc" /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /FD /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "libilbc - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug\ilbc"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /ZI /Od /I "." /I ".." /I "..\contrib\ilbc" /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /FD /GZ /c
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

# Name "libilbc - Win32 Release"
# Name "libilbc - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\contrib\ilbc\anaFilter.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\constants.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\createCB.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\doCPLC.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\enhancer.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\filter.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\FrameClassify.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\gainquant.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\getCBvec.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\helpfun.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\hpInput.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\hpOutput.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iCBConstruct.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iCBSearch.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iLBC_decode.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iLBC_encode.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\LPCdecode.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\LPCencode.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\lsf.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\packing.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\StateConstructW.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\StateSearchW.c
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\syntFilter.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\contrib\ilbc\anaFilter.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\constants.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\createCB.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\doCPLC.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\enhancer.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\filter.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\FrameClassify.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\gainquant.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\getCBvec.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\helpfun.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\hpInput.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\hpOutput.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iCBConstruct.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iCBSearch.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iLBC_decode.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iLBC_define.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\iLBC_encode.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\LPCdecode.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\LPCencode.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\lsf.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\packing.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\StateConstructW.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\StateSearchW.h
# End Source File
# Begin Source File

SOURCE=..\contrib\ilbc\syntFilter.h
# End Source File
# End Group
# End Target
# End Project
