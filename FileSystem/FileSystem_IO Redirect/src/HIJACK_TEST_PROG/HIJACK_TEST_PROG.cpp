#include <windows.h>
#include <stdio.h>

int main(void)
{
    printf("[Kernel-Level DLL-Hijacking]\n");
    HMODULE hDll = LoadLibraryW(L".\\HIJACK_TEST_DLL");

    if (hDll == NULL)
    {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        system("pause");
        return 1;
    }

    printf("Dll loaded successfully.\n");
    // DLL 사용

    FreeLibrary(hDll);
    system("pause");

    return 0;
}