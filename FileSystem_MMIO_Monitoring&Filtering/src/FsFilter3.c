/*++

Module Name:

    FsFilter3.c

Abstract:

    미니필터를 이용한 Memory Mapped I/O 모니터링/차단 구현

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

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION ,
      0,
      PreOperation,
      NULL },
      // ▲  Memory Mapped I/O 뷰 매핑 시, 섹션 객체 동기화 IRP
    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION ,
      0,
      PreOperation,
      NULL },
      // ▲ Memory Mapped I/O 뷰 매핑 시, 섹션 객체 동기화 해제 IRP
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
    FsFilterUnload,                     //  MiniFilterUnload 
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
    DbgPrint("[DRIVER] Load");

    NTSTATUS status;
    status = FltRegisterFilter( DriverObject,
                                &FilterRegistration,
                                &gFilterHandle );

    FLT_ASSERT( NT_SUCCESS( status ) );

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

    DbgPrint("[DRIVER] Unload");
    FltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}

/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
PreOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    } // IRQL 검사


    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION pFileInfo = NULL;

    status = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED, &pFileInfo);
    if (!NT_SUCCESS(status)) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    // IRP에 명시된 파일 경로를 알아냅니다

    status = FltParseFileNameInformation(pFileInfo);
    if (!NT_SUCCESS(status)) {
        FltReleaseFileNameInformation(pFileInfo);
        return FLT_POSTOP_FINISHED_PROCESSING;
    } // 파일 경로를 파싱하여 구조체 멤버를 채워줍니다. (파일 명, 확장자, ...)

    ULONG AccessPID = FltGetRequestorProcessId(Data);
    // 접근하는 PID를 구합니다.

    switch (Data->Iopb->MajorFunction) {
        case IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION: {
            DbgPrint("[DRIVER] File Request : PID(%lu)\tIRP(ACQUIRE_SECTION)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
            break;
        } // Memory Mapped I/O 뷰 매핑 시, 섹션 객체 동기화 IRP (차단용)

        case IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION: {
            DbgPrint("[DRIVER] File Request : PID(%lu)\tIRP(RELEASE_SECTION)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
            break;
        } // Memory Mapped I/O 뷰 매핑 시, 섹션 객체 동기화 해제 IRP (모니터링용)
    }

    UNICODE_STRING CmpFilePath = RTL_CONSTANT_STRING(L"*\\TEST.TXT");
    BOOLEAN result = FsRtlIsNameInExpression(&CmpFilePath, &pFileInfo->Name, TRUE, NULL);
    // 대소문자 구분없이 파일 경로에 test.txt이 매칭되는지 확인합니다.

    if (pFileInfo != NULL) {
        FltReleaseFileNameInformation(pFileInfo);
        pFileInfo = NULL;
    } // FltGetFileNameInformation 함수로 할당받은 파일 경로가 있는 경우, 해제합니다. 

    if (result) {
        DbgPrint("[DRIVER] test.txt Denied.\n");
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
        // test.txt에 매칭된다면 IRP를 거부합니다.
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
    // 위 조건에 해당하지 않으면 IRP를 허용합니다. 
}
