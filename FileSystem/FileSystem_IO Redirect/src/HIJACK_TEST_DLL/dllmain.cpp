// dllmain.cpp : DLL 애플리케이션의 진입점을 정의합니다.
#include "pch.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
#ifdef _DEBUG
        MessageBoxW(
            NULL,
            L"TEST",
            L"TEST",
            MB_OK | MB_ICONINFORMATION
        );
#else
        MessageBoxW(
            NULL,
            L"DLL-Hijacking Success!",
            L"HIJACK",
            MB_OK | MB_ICONERROR
        );
#endif
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

