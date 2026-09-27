#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define DRIVER_IOCTL    0x100
#define DRIVER_IOCTL_PRINT_TEST 					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0000, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DRIVER_IOCTL_UNLOAD_DRIVER					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0001, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _DATA {
    WCHAR InBuf[255] = L"Hello Kernel!";
    WCHAR OutBuf[255] = L"";
} DATA, *PDATA;

int main()
{
    HANDLE hDevice = CreateFile(L"\\\\.\\IOCTL_DRIVER",
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device: %d\n", GetLastError());
        return 1;
    }
    // IOCTL 연결
    
    DWORD bytesReturned;
    BOOL result = FALSE;
    DATA Message;

    printf("[1. Message Test]\n");
    result = DeviceIoControl(hDevice,
        DRIVER_IOCTL_PRINT_TEST,
        &Message,
        sizeof(Message),
        &Message,
        sizeof(Message),
        &bytesReturned,
        NULL);
    if (result) {
        printf("Input Msg : %ls\n", Message.InBuf);
        printf("Output Msg : %ls\n", Message.OutBuf);
    } else {
        printf("IOCTL failed: %d\n", GetLastError());
    }
    printf("\n");
    system("pause");
    // DRIVER_IOCTL_PRINT_TEST 테스트


    printf("\n\n[2. Unloadable Test]\n");
    result = DeviceIoControl(hDevice,
        DRIVER_IOCTL_UNLOAD_DRIVER,
        NULL,
        sizeof(NULL),
        NULL,
        0,
        &bytesReturned,
        NULL);
    if (result)
        printf("Sent to driver successfully!\n");
    else
        printf("IOCTL failed: %d\n", GetLastError());
    printf("\n");
    // DRIVER_IOCTL_UNLOAD_DRIVER 테스트

    CloseHandle(hDevice);
    // IOCTL 연결 해제
    system("pause");
    return 0;
}