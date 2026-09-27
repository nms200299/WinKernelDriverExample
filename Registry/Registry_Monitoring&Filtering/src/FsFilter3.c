/*++

Module Name:

   .c

Abstract:

    CmRegisterCallbackEx을 이용한 레지스트리 모니터링 및 차단

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

BOOLEAN RegistryNotifyAble = FALSE;
LARGE_INTEGER CallbackCookie;


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

PUNICODE_STRING GetRegObjName(PVOID Object, PUNICODE_STRING CombineString)
{
    NTSTATUS status;
    ULONG size = 0;
    POBJECT_NAME_INFORMATION nameInfo;

    status = ObQueryNameString(Object, NULL, 0, &size);
    if (status != STATUS_INFO_LENGTH_MISMATCH) {
        return NULL;
    } // 레지스트리 오브젝트 이름의 크기를 알아냄


    nameInfo = (POBJECT_NAME_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, size, 'pAtH');
    if (!nameInfo) return NULL;
    RtlZeroMemory(nameInfo, size);

    status = ObQueryNameString(Object, nameInfo, size, &size);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(nameInfo, 'pAtH');
        return NULL;
    } // 레지스트리 오브젝트 이름을 알아냄

    if (CombineString) {
        size += CombineString->Length + (sizeof(WCHAR) * 2);
        // 결합할 문자열이 존재하면 할당 받을 크기에 결합할 문자열 크기 + NULL을 더함
    }

    PUNICODE_STRING regPath = NULL;
    regPath = (PUNICODE_STRING)ExAllocatePoolWithTag(NonPagedPool, sizeof(UNICODE_STRING), 'pAtH');
    if (!regPath) {
        ExFreePoolWithTag(nameInfo, 'pAtH');
        return NULL;
    }
    RtlZeroMemory(regPath, sizeof(UNICODE_STRING));
    // 레지스트리 경로를 저장할 UNICODE_STRING 할당 및 초기화

    regPath->Buffer = (PWCH)ExAllocatePoolWithTag(NonPagedPool, size, 'pAtH');
    if (!regPath->Buffer) {
        ExFreePoolWithTag(regPath, 'pAtH');
        ExFreePoolWithTag(nameInfo, 'pAtH');
        return NULL;
    }
    RtlZeroMemory(regPath->Buffer, size);
    // 위 UNICODE_STRING의 버퍼 할당 및 초기화

    regPath->MaximumLength = (USHORT)size;
    RtlCopyMemory(regPath->Buffer, nameInfo->Name.Buffer, nameInfo->Name.Length);
    regPath->Length = nameInfo->Name.Length;
    ExFreePoolWithTag(nameInfo, 'pAtH');
    // 레지스트리 오브젝트 이름을 복사

    if ((CombineString) && (CombineString->Length != 0)) {
        RtlCopyMemory(regPath->Buffer + (regPath->Length / sizeof(WCHAR)),
            L"\\",
            sizeof(WCHAR));
        regPath->Length += sizeof(WCHAR);
        RtlCopyMemory(regPath->Buffer + (regPath->Length / sizeof(WCHAR)),
            CombineString->Buffer ,
            CombineString->Length);
        regPath->Length += CombineString->Length;
    } // 결합할 경로 문자열이 존재하면 문자열 결합 시도

    return regPath;
}

NTSTATUS RegistryCallback(
    PVOID CallbackContext,
    PVOID Argument1,
    PVOID Argument2
) {
    UNREFERENCED_PARAMETER(CallbackContext);
    REG_NOTIFY_CLASS notifyClass = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;
    PUNICODE_STRING RegPath1 = NULL;
    PUNICODE_STRING RegPath2 = NULL;

    switch (notifyClass) {
    // ▼ 레지스트리 값 관련 Notification
        case RegNtPreSetValueKey: {
            PREG_SET_VALUE_KEY_INFORMATION info = (PREG_SET_VALUE_KEY_INFORMATION)Argument2;
            RegPath1 = GetRegObjName(info->Object, info->ValueName);
            if (RegPath1) {
                DbgPrint("[RegNtPreSetValueKey] PathName: %wZ\n", RegPath1);
            }
            break;
        }

        case RegNtPreDeleteValueKey: {
            PREG_DELETE_VALUE_KEY_INFORMATION info = (PREG_DELETE_VALUE_KEY_INFORMATION)Argument2;
            RegPath1 = GetRegObjName(info->Object, info->ValueName);
            if (RegPath1) {
                DbgPrint("[RegNtPreDeleteValueKey] PathName: %wZ", RegPath1);
            }
            break;
        }
    
        // ▼ 레지스트리 키 관련 Notification
        case RegNtPreCreateKey: {
            PREG_CREATE_KEY_INFORMATION info = (PREG_CREATE_KEY_INFORMATION)Argument2;
            RegPath1 = GetRegObjName(info->RootObject, info->CompleteName);
            if (RegPath1) {
                DbgPrint("[RegNtPreCreateKey] PathName: %wZ\n", RegPath1);
            }
            break;
        } // Windows 7 이전

        case RegNtPreCreateKeyEx: {
            PREG_CREATE_KEY_INFORMATION_V1 info = (PREG_CREATE_KEY_INFORMATION_V1)Argument2;
            RegPath1 = GetRegObjName(info->RootObject, info->CompleteName);
            if (RegPath1) {
                DbgPrint("[RegNtPreCreateKeyEx] PathName: %wZ\n", RegPath1);
            }
            break;
        } // Windows 7 이후

        case RegNtPreDeleteKey: {
            PREG_DELETE_KEY_INFORMATION info = (PREG_DELETE_KEY_INFORMATION)Argument2;
            RegPath1 = GetRegObjName(info->Object, NULL);
            if (RegPath1) {
                DbgPrint("[RegNtPreDeleteKey] KeyName: %wZ\n", RegPath1);
            }
            break;
        }

        case RegNtPreRenameKey: {
            PREG_RENAME_KEY_INFORMATION info = (PREG_RENAME_KEY_INFORMATION)Argument2;
            RegPath1 = GetRegObjName(info->Object, NULL);
            RegPath2 = info->NewName;
            if (RegPath1 && RegPath2) {
                DbgPrint("[RegNtPreRenameKey] Src: %wZ -> Dst: %wZ\n", RegPath1, RegPath2);
            }
            break;
        }

        case RegNtPreReplaceKey: {
            PREG_REPLACE_KEY_INFORMATION info = (PREG_REPLACE_KEY_INFORMATION)Argument2;
            DbgPrint("[RegNtPreReplaceKey] Src: %wZ -> Dst: %wZ\n",
                info->NewFileName, info->OldFileName);
            DbgPrint("[DRIVER] Registry Access Denied\n");
            return STATUS_ACCESS_DENIED;
        } // 레지스트리 하이브 전체를 교체 (차단)

        default:
            return STATUS_SUCCESS;
    }


    UNICODE_STRING CmpPathExprssion;
    UNICODE_STRING CmpNameExprssion;
    RtlInitUnicodeString(&CmpPathExprssion, L"*\\TEST*");
    RtlInitUnicodeString(&CmpNameExprssion, L"TEST");

    if (RegPath1) {
        BOOLEAN CmpResult = FsRtlIsNameInExpression(&CmpPathExprssion, RegPath1, TRUE, NULL);
        ExFreePoolWithTag(RegPath1->Buffer, 'pAtH');
        ExFreePoolWithTag(RegPath1, 'pAtH');
        RegPath1 = NULL;
        if (CmpResult) {
            DbgPrint("[DRIVER] Registry Access Denied\n");
            return STATUS_ACCESS_DENIED;
        }
    } // 접근 경로 대상 비교

    if (RegPath2) {
        if (FsRtlIsNameInExpression(&CmpNameExprssion, RegPath2, TRUE, NULL)) {
            DbgPrint("[DRIVER] Registry Access Denied\n");
            return STATUS_ACCESS_DENIED;
        }
    } // RegNtPreRenameKey의 NewName의 경우 이름 대상 비교

    return STATUS_SUCCESS;
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

    UNICODE_STRING altitude;
    RtlInitUnicodeString(&altitude, L"400000");

    status = CmRegisterCallbackEx(
        RegistryCallback,
        &altitude,
        DriverObject,
        NULL,
        &CallbackCookie,
        NULL
    );
    // CmRegisterCallbackEx() Callback 등록

    if (NT_SUCCESS(status)) {
        RegistryNotifyAble = TRUE;
        DbgPrint("[DRIVER] CmRegisterCallbackEx Create Success\n");
    } else {
        DbgPrint("[DRIVER] CmRegisterCallbackEx Create Failed: 0x%08X\n", status);
    }

    return status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (RegistryNotifyAble){
        RegistryNotifyAble = FALSE;
        NTSTATUS status;
        status = CmUnRegisterCallback(CallbackCookie);
        // CmRegisterCallbackEx() Callback 삭제
        if (!NT_SUCCESS(status)) {
            DbgPrint("[DRIVER] CmUnRegisterCallback Failed: 0x%08X\n", status);
        } else {
            DbgPrint("[DRIVER] CmUnRegisterCallback Success\n");
        }
    }
}