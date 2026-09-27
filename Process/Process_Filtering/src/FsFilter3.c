/*++

Module Name:

   .c

Abstract:

    PsSetCreateProcessNotifyRoutineEx을 이용한 프로세스 차단

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

BOOLEAN ProcessNotifyAble = FALSE;

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

void ProcessNotify(
    _Inout_     PEPROCESS Process,
    _In_        HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo
) {
    UNREFERENCED_PARAMETER(Process);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return;
    } // IRQL 체크 (ProcessNotify == PASSIVE_LEVEL)

    if (CreateInfo) {
        UNICODE_STRING CmpUnicodeString;
        RtlInitUnicodeString(&CmpUnicodeString, L"*\\NOTEPAD.EXE");

        if (FsRtlIsNameInExpression(&CmpUnicodeString, (PUNICODE_STRING)CreateInfo->ImageFileName, TRUE, NULL)) {
            CreateInfo->CreationStatus = STATUS_UNSUCCESSFUL;
            DbgPrint("[DRIVER] Process Create Denied. [%wZ](%lu)\n", CreateInfo->ImageFileName, HandleToULong(ProcessId));
            return;
        }
        DbgPrint("[DRIVER] Process Created. [%wZ](%lu)\n", CreateInfo->ImageFileName, HandleToULong(ProcessId));
    } else {
        DbgPrint("[DRIVER] Process Terminated. (%lu)\n", HandleToULong(ProcessId));
    } // 프로세스 생성, 종료에 따른 디버그 메시지 출력

    return;
}

/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        DbgPrint("[DRIVER] Driver IRQL Mismatched.\n");
        DbgPrint("[DRIVER] Unload Driver\n");
        return STATUS_UNSUCCESSFUL;
    } // IRQL 체크 (PsSetCreateProcessNotifyRoutineEx == PASSIVE_LEVEL)

    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = UnloadDriver;

    status = PsSetCreateProcessNotifyRoutineEx(ProcessNotify, FALSE);
    // PsSetCreateProcessNotifyRoutineEx() Callback 등록
    if (NT_SUCCESS(status)) {
        ProcessNotifyAble = TRUE;
        DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutineEx Create Success\n");
    } else {
        DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutineEx Create Failed: 0x%08X\n", status);
    }
    return status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (ProcessNotifyAble){
        ProcessNotifyAble = FALSE;
        NTSTATUS status;
        status = PsSetCreateProcessNotifyRoutineEx(ProcessNotify, TRUE);
        // PsSetCreateProcessNotifyRoutineEx() Callback 삭제
        if (!NT_SUCCESS(status)) {
            DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutineEx Remove Failed: 0x%08X\n", status);
        } else {
            DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutineEx Remove Success\n");
        }

    }
}