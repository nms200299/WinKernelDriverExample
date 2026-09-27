/*++

Module Name:

   .c

Abstract:

    미니필터의 Context와 이중 원형리스트를 이용하여
    IRP 연속성 있는 파일 시스템 모니터링 구현

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
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

FLT_POSTOP_CALLBACK_STATUS
PostOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
);

FLT_PREOP_CALLBACK_STATUS
PreOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
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
    { IRP_MJ_CREATE, 0, NULL, PostOperation }, // 파일 핸들 열기
    { IRP_MJ_READ, 0, PreOperation, NULL }, // 파일 데이터 읽기
    { IRP_MJ_WRITE, 0, PreOperation, NULL }, // 파일 데이터 쓰기
    { IRP_MJ_SET_INFORMATION, 0, PreOperation, NULL }, // 파일 이름 변경, 이동, 삭제
    { IRP_MJ_CLOSE, 0, PreOperation, NULL },  // 파일 핸들 닫기 
    { IRP_MJ_OPERATION_END }
};


//
//  context registration
//

typedef struct _CTX_STREAMHANDLE_IRP {
    ULONG   IPR_OPERATION;
    struct _CTX_STREAMHANDLE_IRP* Front;
    struct _CTX_STREAMHANDLE_IRP* Back;
}CTX_STREAMHANDLE_IRP, *PCTX_STREAMHANDLE_IRP;
// IRP 순서를 기록하기 위한 이중 원형 리스트

typedef struct _CTX_STREAMHANDLE {
    UNICODE_STRING FileName;
    ULONG PID;
    PCTX_STREAMHANDLE_IRP pList;
}CTX_STREAMHANDLE, *PCTX_STREAMHANDLE;
// STREAMHANDLE Context 구조체

VOID ReleaseList(PCTX_STREAMHANDLE pContext);

#define CTX_SIZE_STREAMHANDLE	    sizeof(CTX_STREAMHANDLE)
#define CTX_SIZE_STREAMHANDLE_IRP	sizeof(CTX_STREAMHANDLE_IRP)
#define CTX_TAG_STREAMHANDLE	        'TEST'
#define CTX_TAG_STREAMHANDLE_IRP        'LIST'
#define CTX_TAG_FILENAME                'FILE'

void FreeUnicodeString(PUNICODE_STRING String, ULONG Tag) {
    if (String == NULL) return;
    if (String->Buffer == NULL) return;
    if (String->Length == 0) return;
    ExFreePoolWithTag(String->Buffer, Tag);
    String->Buffer = NULL;
    String->Length = 0;
    String->MaximumLength = 0;
}

VOID CtxCleanup(
    PFLT_CONTEXT Context,
    FLT_CONTEXT_TYPE ContextType
){
    if (ContextType == FLT_STREAMHANDLE_CONTEXT) {
        PCTX_STREAMHANDLE pContext = (PCTX_STREAMHANDLE)Context;
        ReleaseList(pContext);
        FreeUnicodeString(&pContext->FileName, CTX_TAG_FILENAME);
    }
}


const FLT_CONTEXT_REGISTRATION CtxRegistration[] = {
    { FLT_STREAMHANDLE_CONTEXT, // ContextType
      0,                        // Flags
      CtxCleanup,               // ContextCleanupCallback
      CTX_SIZE_STREAMHANDLE,    // Size
      CTX_TAG_STREAMHANDLE },   // PoolTag
    { FLT_CONTEXT_END }
};


//
//  This defines what we want to filter with FltMgr
//
CONST FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags
    CtxRegistration,                    //  Context
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
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    status = FltRegisterFilter(DriverObject,
        &FilterRegistration,
        &gFilterHandle);
    FLT_ASSERT(NT_SUCCESS(status));
    if (NT_SUCCESS(status)) {
        status = FltStartFiltering(gFilterHandle);
        if (!NT_SUCCESS(status)) {
            FltUnregisterFilter(gFilterHandle);
        }
    }
    return status;
}

NTSTATUS
FsFilterUnload(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Flags);
    PAGED_CODE();
    FltUnregisterFilter(gFilterHandle);
    return STATUS_SUCCESS;
}
/*************************************************************************
    UserDefined...
*************************************************************************/

VOID InsertList(ULONG IrpType, PCTX_STREAMHANDLE_IRP pCurrentList) {
    if (pCurrentList == NULL) return;

    PCTX_STREAMHANDLE_IRP pNewList = NULL;
    pNewList = ExAllocatePoolWithTag(NonPagedPool, CTX_SIZE_STREAMHANDLE_IRP, CTX_TAG_STREAMHANDLE_IRP);
    if (pNewList == NULL) return;
    RtlZeroMemory(pNewList, CTX_SIZE_STREAMHANDLE_IRP);

    pNewList->IPR_OPERATION = IrpType;

    if (pCurrentList->Back == pCurrentList) {
        pCurrentList->Front = pNewList;
        pCurrentList->Back = pNewList;
        pNewList->Front = pCurrentList;
        pNewList->Back = pCurrentList;
        // 기존 리스트 항목이 하나일 때, 위와 같이 추가한다.
    }
    else {
        pNewList->Front = pCurrentList;
        pNewList->Back = pCurrentList->Back;
        pCurrentList->Back->Front = pNewList;
        pCurrentList->Back = pNewList;
    } // 기존 리스트 항목이 둘 이상일 때, 위와 같이 추가한다.
    return;
}

VOID PrintList(PCTX_STREAMHANDLE pContext) {
    if (pContext == NULL) return;
    if (pContext->pList == NULL) return;
    if ((pContext->FileName.Buffer == NULL) || (pContext->FileName.Length == 0)) return;

    PCTX_STREAMHANDLE_IRP pPrintList = pContext->pList;

    do {
        switch (pPrintList->IPR_OPERATION) {
            case IRP_MJ_CREATE: {
                DbgPrint("[MINIFILTER] FilePath=%wZ / PID=%lu / IRP_MJ_CREATE\n",
                    &pContext->FileName,
                    pContext->PID);
                break;
            }
            case FILE_DELETE_ON_CLOSE: {
                DbgPrint("[MINIFILTER] FilePath=%wZ / PID=%lu / IRP_MJ_CREATE (DELETE)\n",
                    &pContext->FileName,
                    pContext->PID);
                break;
            }
            case IRP_MJ_READ: {
                DbgPrint("[MINIFILTER] FilePath=%wZ / PID=%lu / IRP_MJ_READ\n",
                    &pContext->FileName,
                    pContext->PID);
                break;
            }
            case IRP_MJ_WRITE: {
                DbgPrint("[MINIFILTER] FilePath=%wZ / PID=%lu / IRP_MJ_WRITE\n",
                    &pContext->FileName,
                    pContext->PID);
                break;
            }
            case IRP_MJ_CLOSE: {
                DbgPrint("[MINIFILTER] FilePath=%wZ / PID=%lu / IRP_MJ_CLOSE\n",
                    &pContext->FileName,
                    pContext->PID);
                break;
            }

            case (ULONG)FileRenameInformation:
            case (ULONG)FileRenameInformationBypassAccessCheck:
            case (ULONG)FileRenameInformationEx:
            case (ULONG)FileRenameInformationExBypassAccessCheck: {
                DbgPrint("[MINIFILTER] FilePath=%wZ / PID=%lu / IRP_SET_INFO (MOVE)\n",
                    &pContext->FileName,
                    pContext->PID);
                break;
            }

            case (ULONG)FileDispositionInformation:
            case (ULONG)FileDispositionInformationEx: {
                DbgPrint("[MINIFILTER] FilePath=%wZ / PID=%lu / IRP_SET_INFO (DELETE)\n",
                    &pContext->FileName,
                    pContext->PID);
                break;
            }
        }
        pPrintList = pPrintList->Front;
    } while (pPrintList != pContext->pList);
    return;
}

VOID ReleaseList(PCTX_STREAMHANDLE pContext) {
    if (pContext == NULL) return;
    if (pContext->pList == NULL) return;

    PCTX_STREAMHANDLE_IRP pCurrentList = pContext->pList;
    PCTX_STREAMHANDLE_IRP pNextList = pCurrentList->Front;
   
    while (pNextList != pCurrentList) {
        PCTX_STREAMHANDLE_IRP pTempList = pNextList;
        pNextList = pNextList->Front;
        ExFreePoolWithTag(pTempList, CTX_TAG_STREAMHANDLE_IRP);
    }
    ExFreePoolWithTag(pCurrentList, CTX_TAG_STREAMHANDLE_IRP);
    pContext->pList = NULL;
    return;
}

/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/


FLT_PREOP_CALLBACK_STATUS
PreOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext
) {
    UNREFERENCED_PARAMETER(CompletionContext);

    NTSTATUS status;
    PCTX_STREAMHANDLE pContext = NULL;

    status = FltGetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&pContext);
    if ((!NT_SUCCESS(status)) || (pContext == NULL)) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    // (참조 카운트 : 2)
    FltReleaseContext((PFLT_CONTEXT)pContext); // (참조 카운트 : 1)

    

    switch (Data->Iopb->MajorFunction) {
        case IRP_MJ_READ:
        case IRP_MJ_WRITE: {
            InsertList(Data->Iopb->MajorFunction, pContext->pList);
            break;
        } // READ, WRITE는 리스트에 삽입만 수행

        case IRP_MJ_CLOSE: {
            InsertList(Data->Iopb->MajorFunction, pContext->pList);
            PrintList(pContext);
            FltDeleteStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, NULL);
            break;
        } // CLOSE는 삽입 후, 출력, Context 해제 수행

        case IRP_MJ_SET_INFORMATION: {
            FILE_INFORMATION_CLASS FileInfoClass = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
            PVOID FileInfoBuffer = Data->Iopb->Parameters.SetFileInformation.InfoBuffer;

            switch (FileInfoClass) {
                case FileDispositionInformation: {
                    PFILE_DISPOSITION_INFORMATION DispositionInfo = (PFILE_DISPOSITION_INFORMATION)FileInfoBuffer;
                    if (DispositionInfo->DeleteFile) {
                        InsertList((ULONG)FileInfoClass, pContext->pList);
                    }
                    break;
                } // 파일 삭제 요청(DeleteFile() 등)이면, 리스트에 삽입 수행

                case FileDispositionInformationEx: {
                    PFILE_DISPOSITION_INFORMATION_EX DispositionInfoEx = (PFILE_DISPOSITION_INFORMATION_EX)FileInfoBuffer;
                    if (DispositionInfoEx->Flags != FILE_DISPOSITION_DO_NOT_DELETE) {
                        InsertList((ULONG)FileInfoClass, pContext->pList);
                    }
                    break;
                } // 파일 삭제 요청(NtDeleteFile() 등)이면, 리스트에 삽입 수행

                case FileRenameInformation:
                case FileRenameInformationBypassAccessCheck:
                case FileRenameInformationEx:
                case FileRenameInformationExBypassAccessCheck: {
                    InsertList((ULONG)FileInfoClass, pContext->pList);
                    break;
                } // 파일 이동, 이름 변경 요청 (MoveFile() 등)이면, 리스트에 삽입 수행
            }
            break;
        } //SET_INFORMATION는 조건에 따라 분기하여 삽입 수행
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

//*************************************************************************

FLT_POSTOP_CALLBACK_STATUS
PostOperation(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
) {
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    } // IRQL 검사 (PASSIVE_LEVEL이 아닌 상태에서 Paged-Pool 메모리에 접근하지 못합니다)

    switch(Data->Iopb->MajorFunction) {
        case IRP_MJ_CREATE: {
            NTSTATUS status;
            PFLT_FILE_NAME_INFORMATION pFileInfo = NULL;
            status = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED, &pFileInfo);
            if (!NT_SUCCESS(status)) {
                return FLT_POSTOP_FINISHED_PROCESSING;
            }
            if ((pFileInfo->Name.Buffer == NULL) || (pFileInfo->Name.Length == 0)) {
                FltReleaseFileNameInformation(pFileInfo);
                return FLT_POSTOP_FINISHED_PROCESSING;
            } // 접근하려는 파일 경로를 구합니다.


            PCTX_STREAMHANDLE pContext = NULL;
            status = FltAllocateContext(FltObjects->Filter, FLT_STREAMHANDLE_CONTEXT, CTX_SIZE_STREAMHANDLE, NonPagedPool, (PFLT_CONTEXT*)&pContext);
            if ((!NT_SUCCESS(status)) || (pContext == NULL)) {
                FltReleaseFileNameInformation(pFileInfo);
                return FLT_POSTOP_FINISHED_PROCESSING;
            }
            RtlZeroMemory(pContext, CTX_SIZE_STREAMHANDLE);
            // Context 할당 후 초기화 (참조 카운트 : 1)

                        
            pContext->FileName.Length = pFileInfo->Name.Length;
            pContext->FileName.MaximumLength = pContext->FileName.Length + sizeof(WCHAR);
            pContext->FileName.Buffer = NULL;
            pContext->FileName.Buffer = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)pContext->FileName.MaximumLength, CTX_TAG_FILENAME);
            if (pContext->FileName.Buffer == NULL) {
                FltReleaseContext((PFLT_CONTEXT)pContext); // (참조 카운트 : 0)
                FltReleaseFileNameInformation(pFileInfo);
                return FLT_POSTOP_FINISHED_PROCESSING;
            }
            RtlZeroMemory(pContext->FileName.Buffer, pContext->FileName.MaximumLength);
            RtlCopyMemory(pContext->FileName.Buffer, pFileInfo->Name.Buffer, pFileInfo->Name.Length);
            FltReleaseFileNameInformation(pFileInfo);
            // 접근하려는 파일 경로 설정


            pContext->pList = ExAllocatePoolWithTag(NonPagedPool, CTX_SIZE_STREAMHANDLE_IRP, CTX_TAG_STREAMHANDLE_IRP);
            if (pContext->pList == NULL) {
                FltReleaseContext((PFLT_CONTEXT)pContext); // (참조 카운트 : 0)
                return FLT_POSTOP_FINISHED_PROCESSING;
            }
            RtlZeroMemory(pContext->pList, CTX_SIZE_STREAMHANDLE_IRP);
            ULONG options = Data->Iopb->Parameters.Create.Options;
            if (options & FILE_DELETE_ON_CLOSE) {
                pContext->pList->IPR_OPERATION = FILE_DELETE_ON_CLOSE;
                // 핸들이 종료될 때 파일도 같이 삭제되는 옵션
            } else {
                pContext->pList->IPR_OPERATION = IRP_MJ_CREATE;
            }
            pContext->pList->Front = pContext->pList;
            pContext->pList->Back = pContext->pList;
            // 이중 원형 리스트 설정


            PETHREAD pETHREAD = Data->Thread;
            PEPROCESS pEPROCESS = IoThreadToProcess(pETHREAD);
            pContext->PID = (ULONG)(ULONG_PTR)PsGetProcessId(pEPROCESS);
            // PID 설정


            status = FltSetStreamHandleContext(FltObjects->Instance, FltObjects->FileObject, FLT_SET_CONTEXT_REPLACE_IF_EXISTS, (PFLT_CONTEXT)pContext, NULL);
            if (!NT_SUCCESS(status)) {
                FltReleaseContext((PFLT_CONTEXT)pContext); // (참조 카운트 : 0)
                return FLT_POSTOP_FINISHED_PROCESSING;
            } // Context 등록 (참조 카운트 : 2)
            FltReleaseContext((PFLT_CONTEXT)pContext); // (참조 카운트 : 1)


            break;
        }
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}

