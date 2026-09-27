/*++

Module Name:

   .c

Abstract:

    ObRegisterCallbacks를 이용한 프로세스 보호 기능 구현

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


typedef PCHAR(*GET_PROCESS_IMAGE_NAME) (PEPROCESS Process);
GET_PROCESS_IMAGE_NAME PsGetProcessImageFileName;
PVOID gObRegHandle = NULL;

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


#define PROCESS_ACESS_TERMINATE       0x0001	// TerminateProcess
#define PROCESS_ACESS_VM_OPERATION    0x0008	// VirtualProtect, WriteProcessMemory
#define PROCESS_ACESS_VM_READ         0x0010	// ReadProcessMemory
#define PROCESS_ACESS_VM_WRITE        0x0020	// WriteProcessMemory
#define PROCESS_ACESS_CREATE_THREAD   0x0002    // CreateRemoteThread
#define PROCESS_ACESS_DUP_HANDLE      0x0040    // DuplicateHandle
#define PROCESS_ACESS_SET_QUOTA       0x0100    // SetProcessWorkingSetSize
#define PROCESS_ACESS_SUSPEND_RESUME  0x0800    // SuspendThread

OB_PREOP_CALLBACK_STATUS ObPreCallback(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION pOperationInformation){
    UNREFERENCED_PARAMETER(RegistrationContext);

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return OB_PREOP_SUCCESS;
    } // IRQL 검사 후 PASSIVE_LEVEL 초과면, return
    if (PsGetProcessImageFileName == NULL) {
        return OB_PREOP_SUCCESS;
    } // Undocumented API에 대한 주소가 없으면 return


    NTSTATUS status;
    PCHAR pDstEProcessNameChar = NULL;
    PEPROCESS pDstEProcess = NULL;
    if (pOperationInformation->ObjectType == *PsThreadType) {
        pDstEProcess = IoThreadToProcess((PETHREAD)pOperationInformation->Object);
        // 스레드 객체일 경우, 해당 스레드가 위치한 프로세스 객체를 사용
    } else if (pOperationInformation->ObjectType == *PsProcessType) {
        pDstEProcess = (PEPROCESS)pOperationInformation->Object;
    } else {
        return OB_PREOP_SUCCESS;
    } // 접근 당하는 EPROCESS 개체를 구함

    pDstEProcessNameChar = PsGetProcessImageFileName(pDstEProcess);
    if (!pDstEProcessNameChar) {
        return OB_PREOP_SUCCESS;
    } // 접근 당하는 프로세스 이름을 구할 수 없으면 return

    ANSI_STRING DstEProcessNameString;
    ANSI_STRING DstCmpProcessName;
    RtlInitString(&DstEProcessNameString, pDstEProcessNameChar);
    RtlInitString(&DstCmpProcessName, "notepad.exe");
    // PCHAR을 ANSI_STRING으로 변환

    if (RtlCompareString(&DstEProcessNameString, &DstCmpProcessName, TRUE) != 0) {
        return OB_PREOP_SUCCESS;
    } // 접근 당하는 프로세스 이름이 notepad.exe가 아니면 return


    ULONG DstEProcessID = HandleToULong(PsGetProcessId(pDstEProcess));
    ULONG SrcEProcessID = HandleToULong(PsGetCurrentProcessId());
    if (DstEProcessID == SrcEProcessID) {
        return OB_PREOP_SUCCESS;
    } // 접근하는 PID와 접근 당하는 PID와 동일하면 return

    PEPROCESS pSrcEProcess = PsGetCurrentProcess();
    PUNICODE_STRING SrcEProcessPath = NULL;
    status = SeLocateProcessImageName(pSrcEProcess, &SrcEProcessPath);
    if (!NT_SUCCESS(status)) {
        return OB_PREOP_SUCCESS;
    } // 접근하는 프로세스 경로를 알아내지 못하면 return
    if ((SrcEProcessPath == NULL) || (SrcEProcessPath->Buffer == NULL) || (SrcEProcessPath->Length == 0)) {
        return OB_PREOP_SUCCESS;
    } // 접근하는 프로세스 경로가 비어있으면(시스템 프로세스면) return

    UNICODE_STRING SrcCmpEProcessPattern;
    RtlInitUnicodeString(&SrcCmpEProcessPattern, L"\\DEVICE\\HARDDISKVOLUME*\\WINDOWS\\EXPLORER.EXE");
    if (FsRtlIsNameInExpression(&SrcCmpEProcessPattern, SrcEProcessPath, TRUE, NULL)) {
        ExFreePool(SrcEProcessPath);
        return OB_PREOP_SUCCESS;
    } // 접근하는 프로세스 경로가 explorer.exe면 return
    RtlInitUnicodeString(&SrcCmpEProcessPattern, L"\\DEVICE\\HARDDISKVOLUME*\\WINDOWS\\SYSTEM32\\CSRSS.EXE");
    if (FsRtlIsNameInExpression(&SrcCmpEProcessPattern, SrcEProcessPath, TRUE, NULL)) {
        ExFreePool(SrcEProcessPath);
        return OB_PREOP_SUCCESS;
    } // 접근하는 프로세스 경로가 csrss.exe면 return

    DbgPrint("[DRIVER] Path=(%wZ)[%lu] -> Name(%Z)[%lu]\n", SrcEProcessPath, SrcEProcessID, &DstEProcessNameString, DstEProcessID);
    ExFreePool(SrcEProcessPath);
    // 접근하는 프로세스 경로 할당 해제


    PACCESS_MASK Desired = NULL;
    PACCESS_MASK ChangedDesired = NULL;
    if (pOperationInformation->Operation == OB_OPERATION_HANDLE_CREATE) {
        Desired = &pOperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess;
        ChangedDesired = &pOperationInformation->Parameters->CreateHandleInformation.DesiredAccess;
    }
    else if (pOperationInformation->Operation == OB_OPERATION_HANDLE_DUPLICATE) {
        Desired = &pOperationInformation->Parameters->DuplicateHandleInformation.OriginalDesiredAccess;
        ChangedDesired = &pOperationInformation->Parameters->DuplicateHandleInformation.DesiredAccess;
    }
    else {
        return OB_PREOP_SUCCESS;
    } // 핸들 생성과 복제에 대한 구조체 처리


    if (*Desired & PROCESS_ACESS_TERMINATE){
        *ChangedDesired &= ~PROCESS_ACESS_TERMINATE;
    } // 프로세스 종료 권한 제거
    if (*Desired & PROCESS_ACESS_VM_READ){
        *ChangedDesired &= ~PROCESS_ACESS_VM_READ;
    } // 가상 메모리 공간 읽기 권한 제거
    if (*Desired & PROCESS_ACESS_VM_WRITE){
        *ChangedDesired &= ~PROCESS_ACESS_VM_WRITE;
    } // 가상 메모리 공간 쓰기 권한 제거
    if (*Desired & PROCESS_ACESS_VM_OPERATION) {
        *ChangedDesired &= ~PROCESS_ACESS_VM_OPERATION;
    } // 가상 메모리 작업 권한 제거
    if (*Desired & PROCESS_ACESS_CREATE_THREAD) {
        *ChangedDesired &= ~PROCESS_ACESS_CREATE_THREAD;
    } // 스레드 생성 권한 제거
    if (*Desired & PROCESS_ACESS_DUP_HANDLE) {
        *ChangedDesired &= ~PROCESS_ACESS_DUP_HANDLE;
    } // 핸들 복제 권한 제거
    if (*Desired & PROCESS_ACESS_SET_QUOTA) {
        *ChangedDesired &= ~PROCESS_ACESS_SET_QUOTA;
    } // 메모리 제한 권한 제거
    if (*Desired & PROCESS_ACESS_SUSPEND_RESUME) {
        *ChangedDesired &= ~PROCESS_ACESS_SUSPEND_RESUME;
    } // 스레드 일시중지 권한 제거

    return OB_PREOP_SUCCESS;
}

NTSTATUS ObRegisterInit() {
    if (KeGetCurrentIrql() > APC_LEVEL) {
        DbgPrint("[DRIVER] Driver IRQL Mismatched.\n");
        DbgPrint("[DRIVER] Unload Driver\n");
        return STATUS_UNSUCCESSFUL;
    } // IRQL 체크 (ObRegisterCallbacks <= APC_LEVEL)

    OB_OPERATION_REGISTRATION ObOperationReg[2] = {0, };
    ObOperationReg[0].ObjectType = PsProcessType;
    ObOperationReg[0].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    ObOperationReg[0].PreOperation = ObPreCallback;
    ObOperationReg[0].PostOperation = NULL;
    // Process에 대해 PreOperation을 수행하는 구조체 설정

    ObOperationReg[1].ObjectType = PsThreadType;
    ObOperationReg[1].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    ObOperationReg[1].PreOperation = ObPreCallback;
    ObOperationReg[1].PostOperation = NULL;
    // Thread에 대해 PreOperation을 수행하는 구조체 설정

    OB_CALLBACK_REGISTRATION ObCallbackReg = {0, };
    ObCallbackReg.Version = ObGetFilterVersion();
    ObCallbackReg.OperationRegistrationCount = 2; // ObOperationReg[n]의 개수
    ObCallbackReg.RegistrationContext = NULL;
    ObCallbackReg.OperationRegistration = ObOperationReg;
    RtlInitUnicodeString(&ObCallbackReg.Altitude, L"400000");
    // ObRegister을 등록하는 구조체 설정

    return ObRegisterCallbacks(&ObCallbackReg, &gObRegHandle);
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
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = UnloadDriver;

    UNICODE_STRING sPsGetProcessImageFileName = RTL_CONSTANT_STRING(L"PsGetProcessImageFileName");
    PsGetProcessImageFileName = (GET_PROCESS_IMAGE_NAME)MmGetSystemRoutineAddress(&sPsGetProcessImageFileName);

    status = ObRegisterInit();
    if (NT_SUCCESS(status)) {
        DbgPrint("[DRIVER] ObRegisterCallback Success\n");
    } else {
        DbgPrint("[DRIVER] ObRegisterCallback Failed: 0x%08X\n", status);
    }
    return status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (gObRegHandle){
        ObUnRegisterCallbacks(gObRegHandle);
    }
}