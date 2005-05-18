/**
 * main-service.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yatengine.h"

static void WINAPI ServiceHandler(DWORD code)
{
    TelEngine::Output("ServiceHandler(%u)",code);
}

extern "C" void ServiceMain(DWORD argc, LPTSTR* argv)
{
    RegisterServiceCtrlHandler("yate",ServiceHandler);
    TelEngine::Engine::main(argc,(const char**)argv,0);
}

extern "C" int main(int argc, const char** argv, const char** envp)
{
    static SERVICE_TABLE_ENTRY dispatchTable[] =
    {
	{ TEXT("yate"), (LPSERVICE_MAIN_FUNCTION)ServiceMain },
	{ NULL, NULL }
    };

    return StartServiceCtrlDispatcher(dispatchTable) ? 0 : EINVAL;
}
