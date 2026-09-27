/*++

Module Name:

    FsFilter3.c

Abstract:

    미니필터를 이용한 파일 차단 및 파일 모니터링 구현

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
FsFilter3PreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );


FLT_POSTOP_CALLBACK_STATUS
FsFilter3PostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
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
      FsFilter3PreOperation,
      FsFilter3PostOperation },
      // ▲ 파일 핸들 열기
    { IRP_MJ_READ,
      0,
      FsFilter3PreOperation,
      FsFilter3PostOperation },
      // ▲ 파일 데이터 읽기
    { IRP_MJ_WRITE,
      0,
      FsFilter3PreOperation,
      FsFilter3PostOperation },
      // ▲ 파일 데이터 쓰기
    { IRP_MJ_SET_INFORMATION ,
      0,
      FsFilter3PreOperation,
      FsFilter3PostOperation },
      // ▲ 파일 이름 변경, 이동, 삭제
    { IRP_MJ_CLOSE,
      0,
      FsFilter3PreOperation,
      FsFilter3PostOperation },
      // ▲ 파일 핸들 닫기 
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
    NTSTATUS status;

    UNREFERENCED_PARAMETER( RegistryPath );

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

    FltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}

/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
FsFilter3PreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )

{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    } // IRQL 검사 (PASSIVE_LEVEL이 아닌 상태에서 Paged-Pool 메모리에 접근하지 못합니다)

    NTSTATUS status;
    PFLT_FILE_NAME_INFORMATION pFileInfo = NULL;
    UNICODE_STRING DstFilePath;
    PUNICODE_STRING pOrgFilePath = NULL;
    

    ULONG AccessPID = FltGetRequestorProcessId(Data);
    // 접근하는 PID를 구합니다.

    switch (Data->Iopb->MajorFunction) {
        case IRP_MJ_SET_INFORMATION: {
        // 파일 복사, 이동의 경우, 목적지 경로에 명시된 파일 이름이 변경될 수 있어 아래 로직에서 처리합니다.  
            switch (Data->Iopb->Parameters.SetFileInformation.FileInformationClass) {
                case FileRenameInformation:
                case FileRenameInformationBypassAccessCheck: {
                    PFILE_RENAME_INFORMATION pRenameInfo = (PFILE_RENAME_INFORMATION)Data->Iopb->Parameters.SetFileInformation.InfoBuffer;
                    if ((pRenameInfo == NULL) || (pRenameInfo->FileNameLength == 0)) {
                        return FLT_PREOP_SUCCESS_NO_CALLBACK;
                    } // 목적지 경로를 알 수 없는 경우, 해당 IRP를 허용합니다. (PostOperation 미수행)

                    DstFilePath.Length = (USHORT)pRenameInfo->FileNameLength;
                    DstFilePath.MaximumLength = DstFilePath.Length;
                    DstFilePath.Buffer = pRenameInfo->FileName;
                    pOrgFilePath = &DstFilePath;
                    // IRP의 목적지 경로를 비교 대상으로 삼습니다.

                    DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(RENAME)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, pOrgFilePath);
                    break;
                }
            }
        }


        default: {
            if (pOrgFilePath != NULL) break;
            // 위 RENAME 조건에 매칭되면 아래 로직을 수행하지 않습니다.

            status = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED, &pFileInfo);
            // IRP에 명시된 파일 경로를 알아냅니다

            if ((!NT_SUCCESS(status)) || (pFileInfo == NULL)) {
                return FLT_PREOP_SUCCESS_NO_CALLBACK;
            } // 파일 경로를 알 수 없으면 해당 IRP를 허용합니다. (PostOperation 미수행)

            if (Data->Iopb->MajorFunction == IRP_MJ_CLOSE) {
                status = FltParseFileNameInformation(pFileInfo);
                if (!NT_SUCCESS(status)) {
                    FltReleaseFileNameInformation(pFileInfo);
                    return FLT_PREOP_SUCCESS_NO_CALLBACK;
                }
                // 파일 경로를 파싱하여 구조체 멤버를 채워줍니다. (파일 명, 확장자, ...)

                DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(CLOSE)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
            } // 파일 핸들 닫기 요청 (CloseHandle())

            pOrgFilePath = &pFileInfo->Name;
            break;
        }
    }

    UNICODE_STRING CmpFilePath = RTL_CONSTANT_STRING(L"*\\TEST.TXT");
    BOOLEAN result = FsRtlIsNameInExpression(&CmpFilePath, pOrgFilePath, TRUE, NULL);
    // 대소문자 구분없이 파일 경로에 test.txt이 매칭되는지 확인합니다.
    if (pFileInfo != NULL) {
        FltReleaseFileNameInformation(pFileInfo);
        pFileInfo = NULL;
    } // FltGetFileNameInformation 함수로 할당받은 파일 경로가 있는 경우, 해제합니다. 

    if (result) {
        DbgPrint("[MINIFILTER] test.txt Denied.\n");
        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;
        return FLT_PREOP_COMPLETE;
        // test.txt에 매칭된다면 IRP를 거부합니다.
    }

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
    // 위 조건에 해당하지 않으면 IRP를 허용합니다. (PostOperation 수행)
}


FLT_POSTOP_CALLBACK_STATUS
FsFilter3PostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    )
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );
    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    } // IRQL 검사 (PASSIVE_LEVEL이 아닌 상태에서 Paged-Pool 메모리에 접근하지 못합니다)


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
    }
    // 파일 경로를 파싱하여 구조체 멤버를 채워줍니다. (파일 명, 확장자, ...)

    ULONG AccessPID = FltGetRequestorProcessId(Data);
    // 접근하는 PID를 구합니다.

    switch (Data->Iopb->MajorFunction) {
        case IRP_MJ_CREATE: {
            DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(CREATE)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
            break;
        } // 파일 핸들 열기 요청 (CreateFile())

        case IRP_MJ_READ: {
            DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(READ)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
            break;
        } // 파일 읽기 요청 (ReadFile())

        case IRP_MJ_WRITE: {
            DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(WRITE)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
            break;
        } // 파일 쓰기 요청 (WriteFile())

        case IRP_MJ_SET_INFORMATION: {
            FILE_INFORMATION_CLASS FileInfoClass = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
            PVOID FileInfoBuffer = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;

            switch (FileInfoClass) {
                case FileDispositionInformation: {
                    PFILE_DISPOSITION_INFORMATION DispositionInfo = (PFILE_DISPOSITION_INFORMATION)FileInfoBuffer;
                    if (DispositionInfo->DeleteFile) {
                        DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(DELETE)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
                    }
                    break;
                } // 파일 삭제 요청 (DeleteFile() 등)

                case FileDispositionInformationEx: {
                    PFILE_DISPOSITION_INFORMATION_EX DispositionInfoEx = (PFILE_DISPOSITION_INFORMATION_EX)FileInfoBuffer;
                    if (DispositionInfoEx->Flags != FILE_DISPOSITION_DO_NOT_DELETE) {
                        DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(DELETE_EX)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
                    }
                } // 파일 삭제 요청 (NtDeleteFile() 등) 

                case FileRenameInformation:
                case FileRenameInformationBypassAccessCheck:
                case FileRenameInformationEx:
                case FileRenameInformationExBypassAccessCheck: {
                    DbgPrint("[MINIFILTER] File Request : PID(%lu)\tIRP(MOVE)\tPath(%wZ)\n", (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->FinalComponent);
                } // 파일 이동, 이름 변경 요청 (MoveFile() 등) 
            }
        }
    }

    FltReleaseFileNameInformation(pFileInfo);
    return FLT_POSTOP_FINISHED_PROCESSING;
}
