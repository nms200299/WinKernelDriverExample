/*++

Module Name:

   .c

Abstract:

    PsSetLoadImageNotifyRoutine을 이용한 PE 이미지 모니터링 및 프로세스 차단

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

BOOLEAN ImageNotifyAble = FALSE;

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

BOOLEAN TerminateProcess(HANDLE pid) {
    HANDLE hProcess = NULL;
    OBJECT_ATTRIBUTES objAttr;
    CLIENT_ID clientId;

    clientId.UniqueProcess = pid;
    clientId.UniqueThread = NULL;
    InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS status = ZwOpenProcess(&hProcess, PROCESS_ALL_ACCESS, &objAttr, &clientId);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }
    status = ZwTerminateProcess(hProcess, STATUS_ACCESS_DENIED);
    ZwClose(hProcess);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }
    return TRUE;
} // 프로세스 종료 함수


void PloadImageNotifyRoutine(
    _In_opt_       PUNICODE_STRING FullImageName,
    _In_           HANDLE ProcessId,
    _In_           PIMAGE_INFO ImageInfo
){
    UNREFERENCED_PARAMETER(ProcessId);
    UNREFERENCED_PARAMETER(ImageInfo);

    if (FullImageName == NULL) {
        return;
    } // 이미지 경로를 구할 수 없으면 return

    ULONG PID = HandleToULong(ProcessId);
    DbgPrint("[DRIVER] ImagePath=%wZ / PID=%lu", FullImageName, PID);

    if (PID == 0 ) {
        return;
    }  // PE 이미지가 드라이버면 return
    // (드라이버는 System 프로세스로 실행)

    UNICODE_STRING CmpUnicodeString;
    RtlInitUnicodeString(&CmpUnicodeString, L"*\\NOTEPAD.EXE");
    if (FsRtlIsNameInExpression(&CmpUnicodeString, FullImageName, TRUE, NULL)) {
        if (TerminateProcess(ProcessId)) {
            DbgPrint("[DRIVER] Process Terminated. [%wZ](%lu)\n", FullImageName, PID);
        } else {
            DbgPrint("[DRIVER] Process Terminate Failed. [%wZ](%lu)\n", FullImageName, PID);
        }
    } // 메모장 실행 파일이 로드되면 해당 프로세스 종료

    RtlInitUnicodeString(&CmpUnicodeString, L"*\\DBGHELP.DLL");
    if (FsRtlIsNameInExpression(&CmpUnicodeString, FullImageName, TRUE, NULL)) {
        if (TerminateProcess(ProcessId)) {
            DbgPrint("[DRIVER] Process Terminated. [%wZ](%lu)\n", FullImageName, PID);
        }
        else {
            DbgPrint("[DRIVER] Process Terminate Failed. [%wZ](%lu)\n", FullImageName, PID);
        }
    } // 치트 엔진이 사용하는 DLL이 로드되면 프로세스 종료
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
    } // IRQL 체크 (PsSetLoadImageNotifyRoutine == PASSIVE_LEVEL)

    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = UnloadDriver;


    status = PsSetLoadImageNotifyRoutine(&PloadImageNotifyRoutine);
    // PsSetLoadImageNotifyRoutine() Callback 등록

    if (NT_SUCCESS(status)) {
        ImageNotifyAble = TRUE;
        DbgPrint("[DRIVER] PsSetLoadImageNotifyRoutine Create Success\n");
    } else {
        DbgPrint("[DRIVER] PsSetLoadImageNotifyRoutine Create Failed: 0x%08X\n", status);
    }

    return status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (ImageNotifyAble){
        ImageNotifyAble = FALSE;
        NTSTATUS status;
        status = PsRemoveLoadImageNotifyRoutine(&PloadImageNotifyRoutine);
        // PsSetCreateThreadNotifyRoutine() Callback 삭제
        if (!NT_SUCCESS(status)) {
            DbgPrint("[DRIVER] PsRemoveLoadImageNotifyRoutine Failed: 0x%08X\n", status);
        } else {
            DbgPrint("[DRIVER] PsRemoveLoadImageNotifyRoutine Success\n");
        }
    }
}