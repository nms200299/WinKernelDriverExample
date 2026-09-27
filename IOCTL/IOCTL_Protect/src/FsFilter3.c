/*++

Module Name:

   .c

Abstract:

    IOCTL 접근 제어를 통한 커널 드라이버 보호

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
#define DRIVER_IOCTL_PROTECT_TEST 					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0000, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS IsTrustedProcessByIrp(PIRP Irp) {
    NTSTATUS status = STATUS_SUCCESS;
    PEPROCESS pEProcess = IoGetRequestorProcess(Irp);
    // IRP 생성 프로세스의 EPROCESS 객체를 구함

    PUNICODE_STRING pEProcessPath = NULL;
    status = SeLocateProcessImageName(pEProcess, &pEProcessPath);
    if (!NT_SUCCESS(status) || (pEProcessPath == NULL) || (pEProcessPath->Buffer == NULL)) {
        DbgPrint("[DRIVER] IOCTL - SeLocateProcessImageName Fail\n");
        return STATUS_ACCESS_DENIED;
    } // IRP 생성 프로세스의 전체 경로를 구함

    UNICODE_STRING CmpPathPattern = RTL_CONSTANT_STRING(L"\\DEVICE\\HARDDISKVOLUME*\\ALLOW.EXE");
    if (FsRtlIsNameInExpression(&CmpPathPattern, pEProcessPath, TRUE, NULL)) {
        ExFreePool(pEProcessPath);
        DbgPrint("[DRIVER] IOCTL - Connected User-Application\n");
        return STATUS_SUCCESS;
    } // 접근하는 프로세스 경로가 [ROOT_PATH]\ALLOW.EXE면 return

    ExFreePool(pEProcessPath);
    DbgPrint("[DRIVER] IOCTL - Denied User-Application\n");
    return STATUS_ACCESS_DENIED;
}

NTSTATUS Dispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION IrpStack = NULL;
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    // IRP 스택을 구함

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    // IRP의 기본 반환 값 지정

    switch (IrpStack->MajorFunction) {
        case IRP_MJ_CREATE: {
            Irp->IoStatus.Status = IsTrustedProcessByIrp(Irp);
            break;
        } // 핸들 생성 처리
        case IRP_MJ_CLOSE: {
            DbgPrint("[DRIVER] IOCTL - Disconnected User-Application\n");
            break;
        } // 핸들 종료 처리
        case IRP_MJ_DEVICE_CONTROL: {
            DbgPrint("[DRIVER] IOCTL - Recive I/O Message\n");
            break;
        } // IOCTL 메시지 처리
    }

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
    DriverObject->DriverUnload = UnloadDriver;
    // 드라이버 언로드 콜백 지정

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