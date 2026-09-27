/*++

Module Name:

   .c

Abstract:

    PsSetCreateThreadNotifyRoutine을 이용한 스레드 모니터링

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


typedef PCHAR(*GET_PROCESS_IMAGE_NAME) (PEPROCESS Process);
GET_PROCESS_IMAGE_NAME PsGetProcessImageFileName;
BOOLEAN ThreadNotifyAble = FALSE;

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

void ThreadNotify(
    _In_ HANDLE ProcessId,
    _In_ HANDLE ThreadId,
    _In_ BOOLEAN Create
) {
    if (KeGetCurrentIrql() > APC_LEVEL) {
        return;
    } // IRQL 체크 (ThreadNotify <= APC_LEVEL)

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
        DbgPrint("[DRIVER] Thread Created. [%s](PID=%lu)(TID=%lu)\n", pEProcessNameChar, HandleToULong(ProcessId), HandleToULong(ThreadId));
    } else {
        DbgPrint("[DRIVER] Thread Terminated. [%s](PID=%lu)(TID=%lu)\n", pEProcessNameChar, HandleToULong(ProcessId), HandleToULong(ThreadId));
    } // 스레드 생성, 종료에 따른 디버그 메시지 출력

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
    } // IRQL 체크 (PsSetCreateThreadNotifyRoutine == PASSIVE_LEVEL)

    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = UnloadDriver;

    PsGetProcessImageFileName = NULL;
    UNICODE_STRING sPsGetProcessImageFileName = RTL_CONSTANT_STRING(L"PsGetProcessImageFileName");
    PsGetProcessImageFileName = (GET_PROCESS_IMAGE_NAME)MmGetSystemRoutineAddress(&sPsGetProcessImageFileName);
    // PsGetProcessImageFileName() API 동적 로드

    status = PsSetCreateThreadNotifyRoutine(ThreadNotify);
    // PsSetCreateThreadNotifyRoutine() Callback 등록
    if (NT_SUCCESS(status)) {
        ThreadNotifyAble = TRUE;
        DbgPrint("[DRIVER] PsSetCreateThreadNotifyRoutine Create Success\n");
    } else {
        DbgPrint("[DRIVER] PsSetCreateThreadNotifyRoutine Create Failed: 0x%08X\n", status);
    }

    return status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (ThreadNotifyAble){
        ThreadNotifyAble = FALSE;
        NTSTATUS status;
        status = PsRemoveCreateThreadNotifyRoutine(ThreadNotify);
        // PsSetCreateThreadNotifyRoutine() Callback 삭제
        if (!NT_SUCCESS(status)) {
            DbgPrint("[DRIVER] PsRemoveCreateThreadNotifyRoutine Failed: 0x%08X\n", status);
        } else {
            DbgPrint("[DRIVER] PsRemoveCreateThreadNotifyRoutine Success\n");
        }

    }
}