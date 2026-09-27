/*++

Module Name:

   .c

Abstract:

    IOCTL를 이용한 User-Application 통신 예제

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

/*************************************************************************
    Prototypes
*************************************************************************/

EXTERN_C_START
DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );
VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject);
EXTERN_C_END


/*************************************************************************
    UserDefined ...
*************************************************************************/

#define DRIVER_IOCTL    0x100
#define DRIVER_IOCTL_PRINT_TEST 					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0000, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DRIVER_IOCTL_UNLOAD_DRIVER					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0001, METHOD_BUFFERED, FILE_ANY_ACCESS)

PDRIVER_OBJECT gDriverObject;
WCHAR TEST_MSG[] = L"Hello User!";

typedef struct _DATA {
    WCHAR InBuf[255];
    WCHAR OutBuf[255];
} DATA, *PDATA;

ULONG DeviceControlDispatch(PIRP Irp, PIO_STACK_LOCATION IrpStack) {
    ULONG IoControlCode = IrpStack->Parameters.DeviceIoControl.IoControlCode;

    switch (IoControlCode) {
        case DRIVER_IOCTL_PRINT_TEST: {
            if (IrpStack->Parameters.DeviceIoControl.InputBufferLength > 2000) break;
            // Buffer 길이 검사
            PDATA Message = (PDATA)Irp->AssociatedIrp.SystemBuffer;
            DbgPrint("[DRIVER] IOCTL - Input Msg : %ls\n", Message->InBuf);
            // Input Message 출력
            RtlCopyMemory(Message->OutBuf, TEST_MSG , sizeof(TEST_MSG));
            DbgPrint("[DRIVER] IOCTL - Output Msg : %ls\n", Message->OutBuf);
            // Output Message 설정
            return sizeof(DATA);
            break;
        } // 드라이버 테스트 문구 출력

        case DRIVER_IOCTL_UNLOAD_DRIVER: {
            DbgPrint("[DRIVER] IOCTL - Driver Unloadable.\n");
            gDriverObject->DriverUnload = UnloadDriver;
            break;
        } // 드라이버 언로드 콜백 지정
    }

    return 0;
}


NTSTATUS Dispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION IrpStack = NULL;
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    // IRP 스택을 구함

    Irp->IoStatus.Information = 0;
    // 기본적으로 출력 버퍼 없음으로 지정

    switch (IrpStack->MajorFunction) {
        case IRP_MJ_CREATE: {
            DbgPrint("[DRIVER] IOCTL - Connected User-Application\n");
            break;
        } // 핸들 생성 처리
        case IRP_MJ_CLOSE: {
            DbgPrint("[DRIVER] IOCTL - Disconnected User-Application\n");
            break;
        } // 핸들 종료 처리
        case IRP_MJ_DEVICE_CONTROL: {
            Irp->IoStatus.Information = DeviceControlDispatch(Irp, IrpStack);
            break;
        } // IOCTL 메시지 처리
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    // Blind SQLI와 같이 반환 값을 통한 로직 유추 방지를 위해 하나의 값으로 고정

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    // IRP 완료 처리
    return STATUS_SUCCESS;
}


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

#define DIRVER_NAME	L"IOCTL_DRIVER"
UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\"DIRVER_NAME);
UNICODE_STRING DeviceSymName = RTL_CONSTANT_STRING(L"\\DosDevices\\"DIRVER_NAME);

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS Status;
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[DRIVER] Driver Load.\n");
    gDriverObject = DriverObject;

    DriverObject->DriverUnload = NULL;
    // 드라이버 종료 방지 설정

    PDEVICE_OBJECT DeviceObject = NULL;
    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status) || (DeviceObject == NULL)) {
        DbgPrint("[DRIVER] IoCreateDevice Fail");
        return Status;
    } // 디바이스 커널 오브젝트 생성

    Status = IoCreateSymbolicLink(&DeviceSymName, &DeviceName);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] IoCreateSymbolicLink Fail");
        IoDeleteDevice(DeviceObject);
        return Status;
    } // User-Mode에서 접근할 수 있는 심볼릭 링크 생성

    for (INT i=0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
        DriverObject->MajorFunction[i] = Dispatch;
    // Dispatch 콜백 설정

    DeviceObject->Flags |= DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    // 디바이스 오브젝트 BUFFERED_IO 통신 설정

    return Status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject)
{
    IoDeleteSymbolicLink(&DeviceSymName);
    IoDeleteDevice(DriverObject->DeviceObject);
    // 디바이스 관련 오브젝트 제거

    DbgPrint("[DRIVER] Driver Unload.\n");
}