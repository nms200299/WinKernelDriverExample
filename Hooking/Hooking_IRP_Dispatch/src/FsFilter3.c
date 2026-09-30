/*++

Module Name:

   .c

Abstract:

    IRP Dispatch Hooking

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#include <ntstrsafe.h>
#pragma warning(disable: 4996) // ExAllocatePool deprecated
#define DRIVER_IOCTL    0x100
#define DRIVER_IOCTL_PRINT_TEST 					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0000, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DRIVER_IOCTL_UNLOAD_DRIVER					CTL_CODE(FILE_DEVICE_UNKNOWN, DRIVER_IOCTL + 0x0001, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS NTAPI ObReferenceObjectByName(PUNICODE_STRING ObjectName,
    ULONG Attributes,
    PACCESS_STATE AccessState,
    ACCESS_MASK DesiredAccess,
    POBJECT_TYPE ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID ParseContext OPTIONAL,
    PVOID* Object);

POBJECT_TYPE ObGetObjectType(PVOID Object);


/*************************************************************************
    Prototypes
*************************************************************************/
DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );
VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject);
PDRIVER_OBJECT SearchDriverObject(
    PUNICODE_STRING pUniDriverName,
    PDRIVER_OBJECT pDriverObject
);
BOOLEAN IrpHookStart(unsigned char MajorFunction);
VOID IrpHookStop(unsigned char MajorFunction);
NTSTATUS HookDeviceControlDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);


typedef struct _DATA {
    WCHAR InBuf[255];
    WCHAR OutBuf[255];
} DATA, *PDATA;

PDRIVER_DISPATCH g_fnOrgDispatch[IRP_MJ_MAXIMUM_FUNCTION + 1] = { 0, };
PDRIVER_OBJECT g_pTargetDriverObject = NULL;

/*************************************************************************
    initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("[IRP_HOOK] Driver Loaded\n");
    DriverObject->DriverUnload = UnloadDriver;

    UNICODE_STRING uniTargetDriverName = RTL_CONSTANT_STRING(L"\\FileSystem\\IOCTL");
    g_pTargetDriverObject = SearchDriverObject(&uniTargetDriverName, DriverObject);
    if (!g_pTargetDriverObject) {
        DbgPrint("[IRP_HOOK] Not Found %wZ Error !\n", &uniTargetDriverName);
        return STATUS_UNSUCCESSFUL;
    }
    DbgPrint("[IRP_HOOK] Found %wZ Success !\n", &uniTargetDriverName);
    // IOCTL 드라이버의 DRIVER_OBJECT를 찾음

    if (!IrpHookStart(IRP_MJ_DEVICE_CONTROL)) {
        DbgPrint("[IRP_HOOK] IrpHook Failed !\n");
        return STATUS_UNSUCCESSFUL;
    } // IRP_MJ_DEVICE_CONTROL 후킹

    DbgPrint("[IRP_HOOK] IrpHook Successed !\n");
    return status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject) {   
    UNREFERENCED_PARAMETER(DriverObject);
    IrpHookStop(IRP_MJ_DEVICE_CONTROL);
    DbgPrint("[IRP_HOOK] Driver Unloaded\n");
    // 드라이버 종료 시, 언후킹
}

/*************************************************************************
UserDefined ...
*************************************************************************/

// 타겟 드라이버의 드라이버 오브젝트를 찾아서 반환
PDRIVER_OBJECT SearchDriverObject(
    PUNICODE_STRING pUniDriverName,
    PDRIVER_OBJECT pDriverObject
) {
    NTSTATUS status;
    PDRIVER_OBJECT pTargetObject;
    POBJECT_TYPE TypeObjectType = ObGetObjectType(pDriverObject);
    // 드라이버 오브젝트 타입에 해당하는 값을 가져옴
    // (Undocumented API로, 타입 값이 바뀔 수 있기에 함수 사용)

    status = ObReferenceObjectByName(pUniDriverName, OBJ_CASE_INSENSITIVE, NULL, 0, TypeObjectType, KernelMode, NULL, &pTargetObject);
    if (status != STATUS_SUCCESS) return NULL;
    // 해당 드라이버 오브젝트의 주소를 가져옴 (Undocumented API)

    ObDereferenceObject(pTargetObject);
    // 참조 카운트를 낮춤
    return pTargetObject;
}


BOOLEAN IrpHookStart(unsigned char MajorFunction) {
    if (!g_pTargetDriverObject) return FALSE;
    // 목표 드라이버 오브젝트가 존재하지 않으면 리턴
    if (g_fnOrgDispatch[MajorFunction]) return FALSE;
    // 이미 후킹된 IRP Dispatch Routine이면 리턴

    g_fnOrgDispatch[MajorFunction] = g_pTargetDriverObject->MajorFunction[MajorFunction];
    // 원본 IRP Dispatch Routine 주소 백업 
    g_pTargetDriverObject->MajorFunction[MajorFunction] = HookDeviceControlDispatch;
    // 목표 드라이버 오브젝트의 Dispatch Routine 변조

    return TRUE;
}


VOID IrpHookStop(unsigned char MajorFunction) {
    if (!g_pTargetDriverObject) return;
    if (g_fnOrgDispatch[MajorFunction]) {
        g_pTargetDriverObject->MajorFunction[MajorFunction] = g_fnOrgDispatch[MajorFunction];
        g_fnOrgDispatch[MajorFunction] = NULL;
    } // 후킹한 테이블을 원상 복구함
}


NTSTATUS HookDeviceControlDispatch(PDEVICE_OBJECT pDeviceObject, PIRP pIrp) {
    DbgPrint("[IRP_HOOK] %s Enter\n", __func__);

    PIO_STACK_LOCATION pIrpStack = NULL;
    pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
    // IRP 스택을 구함

    switch (pIrpStack->Parameters.DeviceIoControl.IoControlCode) {
        case DRIVER_IOCTL_PRINT_TEST: {
            ULONG dwInBufLen = pIrpStack->Parameters.DeviceIoControl.InputBufferLength;
            if (pIrp->AssociatedIrp.SystemBuffer != NULL && dwInBufLen == sizeof(DATA)) {
                PDATA pHookBuffer = (PDATA)pIrp->AssociatedIrp.SystemBuffer;
                RtlStringCchCopyW(
                    pHookBuffer->InBuf,
                    RTL_NUMBER_OF(pHookBuffer->InBuf),
                    L"This Message is hooked\x00"
                );
            }
            break;
        } // InputBuffer 변조

        case DRIVER_IOCTL_UNLOAD_DRIVER: {
            pIrp->IoStatus.Status = STATUS_SUCCESS;
            pIrp->IoStatus.Information = 0;
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
            break;
        } // IRP 완료 처리
    }

    return g_fnOrgDispatch[pIrpStack->MajorFunction](pDeviceObject, pIrp);
    // 원본 IRP Dispatch Routine함수 호출
}