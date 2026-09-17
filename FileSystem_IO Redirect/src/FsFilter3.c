/*++

Module Name:

    FsFilter3.c

Abstract:

    미니필터를 이용한 IO Redirect 구현

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#include <dontuse.h>

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


PFLT_FILTER gFilterHandle;


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

NTSTATUS
FsFilterUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
PreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

EXTERN_C_END

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, FsFilterUnload)
#endif

//
//  operation registration
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE,
      0,
      PreOperation,
      NULL },
      // ▲ 파일 핸들 열기 IRP

      { IRP_MJ_OPERATION_END }
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {
    sizeof( FLT_REGISTRATION ),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags
    NULL,                               //  Context
    Callbacks,                          //  Operation callbacks
    FsFilterUnload,                    //  MiniFilterUnload 
    NULL,                               //  InstanceSetup
    NULL,                               //  InstanceQueryTeardown
    NULL,                               //  InstanceTeardownStart
    NULL,                               //  InstanceTeardownComplete
    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent
};


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER( RegistryPath );
    NTSTATUS status;
    DbgPrint("[DRIVER] Driver Load\n");

    status = FltRegisterFilter( DriverObject,
                                &FilterRegistration,
                                &gFilterHandle );

    if (NT_SUCCESS( status )) {
        status = FltStartFiltering( gFilterHandle );
        if (!NT_SUCCESS( status )) {
            FltUnregisterFilter( gFilterHandle );
        }
    }

    return status;
}

NTSTATUS
FsFilterUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    )
{
    UNREFERENCED_PARAMETER( Flags );
    PAGED_CODE();
    DbgPrint("[DRIVER] Driver Unload\n");
    FltUnregisterFilter(gFilterHandle);

    return STATUS_SUCCESS;
}

/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
PreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )

{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION pFileInfo = NULL;

    if (Data->Iopb->MajorFunction != IRP_MJ_CREATE) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &pFileInfo);
    if ((!NT_SUCCESS(status)) || (pFileInfo == NULL)) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    } // 파일 경로를 알 수 없으면 해당 IRP를 허용합니다. (PostOperation 미수행)

    status = FltParseFileNameInformation(pFileInfo);
    if (!NT_SUCCESS(status)) {
        FltReleaseFileNameInformation(pFileInfo);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    } // 파일 이름 정보를 파싱

#ifdef _DEBUG
    UNICODE_STRING CmpDiskPath = RTL_CONSTANT_STRING(L"\\DEVICE\\HARDDISKVOLUME*\\WINDOWS\\SYSTEM32\\NOTEPAD.EXE");
#else
    UNICODE_STRING CmpDiskPath = RTL_CONSTANT_STRING(L"*\\HIJACK_TEST_DLL.DLL");
#endif
    BOOLEAN result = FsRtlIsNameInExpression(&CmpDiskPath, &pFileInfo->Name, TRUE, NULL);
    // 리다이렉트 파일이 위치한 경로를 비교

    FltReleaseFileNameInformation(pFileInfo);
    // 파일 이름 할당 해제

    if (result) {
        DbgPrint(
            "[DRIVER] IO Dedirect / MF=%02X PID=%lu TID=%lu FO=%p\n",
            Data->Iopb->MajorFunction,
            (ULONG)(ULONG_PTR)PsGetCurrentProcessId(),
            (ULONG)(ULONG_PTR)PsGetCurrentThreadId(),
            Data->Iopb->TargetFileObject
        );
#ifdef _DEBUG
        UNICODE_STRING RedFilePath = RTL_CONSTANT_STRING(L"\\??\\C:\\Windows\\System32\\calc.exe");
#else
        UNICODE_STRING RedFilePath = RTL_CONSTANT_STRING(L"\\??\\C:\\HIJACK_HOOK_DLL.DLL");
#endif
        status = IoReplaceFileObjectName(Data->Iopb->TargetFileObject, RedFilePath.Buffer, RedFilePath.Length);
        // 파일 I/O 경로 수정

        if (NT_SUCCESS(status)) {
            if (Data->Iopb->TargetFileObject->RelatedFileObject != NULL) {
                Data->Iopb->TargetFileObject->RelatedFileObject = NULL;
            } // 상대 경로 오브젝트가 존재하면 이를 초기화

            FltSetCallbackDataDirty(Data);
            // IO 데이터 수정을 Filter Manager에게 알림

            Data->IoStatus.Status = STATUS_REPARSE;
            Data->IoStatus.Information = IO_REPARSE;
            return FLT_PREOP_COMPLETE;
            // 문자열에 매칭되면 리다이렉트합니다.
        }
    }
 
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
    // 위 조건에 해당하지 않으면 IRP를 허용합니다.
}