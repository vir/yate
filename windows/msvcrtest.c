#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

__declspec(dllexport) int __cdecl crt_true(void)
{
    Sleep(50);
    return (clock() != 0);
}
