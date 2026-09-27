/*++

Module Name:

   .c

Abstract:

    PsSetCreateProcessNotifyRoutine을 이용한 프로세스 모니터링

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


typedef PCHAR(*GET_PROCESS_IMAGE_NAME) (PEPROCESS Process);
GET_PROCESS_IMAGE_NAME PsGetProcessImageFileName;
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
    _In_ HANDLE ParentId,
    _In_ HANDLE ProcessId,
    _In_ BOOLEAN Create
) {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return;
    } // IRQL 체크 (ProcessNotify == PASSIVE_LEVEL)

    UNREFERENCED_PARAMETER(ParentId);
    if (PsGetProcessImageFileName == NULL) {
        return;
    } // PsGetProcessImageFileName() 함수 주소를 구할 수 없으면 return

    NTSTATUS status;
    PEPROCESS EProcess = NULL;
    status = PsLookupProcessByProcessId(ProcessId, &EProcess); // EProcess 참조 카운트 감소 필수
    if (!NT_SUCCESS(status)) {
        return;
    } // PID로 EPROCESS를 구할 수 없으면 return

    PCHAR pEProcessNameChar = NULL;
    pEProcessNameChar = PsGetProcessImageFileName(EProcess);
    ObDereferenceObject(EProcess); // // EProcess 참조 카운트 감소
    if (!pEProcessNameChar) {
        return;
    } // 접근 당하는 프로세스 이름을 구할 수 없으면 return

    if (Create) {
        DbgPrint("[DRIVER] Process Created. [%s](%lu)\n", pEProcessNameChar, HandleToULong(ProcessId));
    } else {
        DbgPrint("[DRIVER] Process Terminated. [%s](%lu)\n", pEProcessNameChar, HandleToULong(ProcessId));
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
    } // IRQL 체크 (PsSetCreateProcessNotifyRoutine == PASSIVE_LEVEL)

    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;
    DriverObject->DriverUnload = UnloadDriver;

    PsGetProcessImageFileName = NULL;
    UNICODE_STRING sPsGetProcessImageFileName = RTL_CONSTANT_STRING(L"PsGetProcessImageFileName");
    PsGetProcessImageFileName = (GET_PROCESS_IMAGE_NAME)MmGetSystemRoutineAddress(&sPsGetProcessImageFileName);
    // PsGetProcessImageFileName() API 동적 로드

    status = PsSetCreateProcessNotifyRoutine(ProcessNotify, FALSE);
    // PsSetCreateProcessNotifyRoutine() Callback 등록
    if (NT_SUCCESS(status)) {
        ProcessNotifyAble = TRUE;
        DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutine Create Success\n");
    } else {
        DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutine Create Failed: 0x%08X\n", status);
    }
    return status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (ProcessNotifyAble){
        ProcessNotifyAble = FALSE;
        NTSTATUS status;
        status = PsSetCreateProcessNotifyRoutine(ProcessNotify, TRUE);
        // PsSetCreateProcessNotifyRoutine() Callback 삭제
        if (!NT_SUCCESS(status)) {
            DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutine Remove Failed: 0x%08X\n", status);
        } else {
            DbgPrint("[DRIVER] PsSetCreateProcessNotifyRoutine Remove Success\n");
        }

    }
}