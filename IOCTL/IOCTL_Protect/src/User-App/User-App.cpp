#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define DRIVER_IOCTL    0x100
#define DRIVER_IOCTL_PROTECT_TEST 					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0000, METHOD_BUFFERED, FILE_ANY_ACCESS)

int main()
{
    printf("[IOCTL PROTECT TEST]\n");

    HANDLE hDevice = CreateFile(L"\\\\.\\IOCTL_DRIVER",
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hDevice == INVALID_HANDLE_VALUE) printf("Failed to open device: %d\n", GetLastError());
    else printf("Success open device\n");
    // IOCTL 연결
    
    DWORD bytesReturned;
    BOOL result = FALSE;
    result = DeviceIoControl(hDevice,
        DRIVER_IOCTL_PROTECT_TEST,
        NULL,
        sizeof(NULL),
        NULL,
        0,
        &bytesReturned,
        NULL);
    if (result) printf("Sent to driver successfully!\n");
    else printf("IOCTL failed: %d\n", GetLastError());
    printf("\n");
    // DRIVER_IOCTL_PROTECT_TEST 테스트

    CloseHandle(hDevice);
    // IOCTL 연결 해제
    system("pause");
    return 0;
}