/*++

Module Name:

    FsFilter3.c

Abstract:
    필터 API 런타임 동적 바인딩
    (미니필터를 이용한 MBR 보호 기능 구현)

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#include <dontuse.h>
#include "PfnFltFunction.h"
#include "skCrypter.h"


#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


PFN_FltRegisterFilter gFltRegisterFilter = NULL;
PFN_FltUnregisterFilter gFltUnregisterFilter = NULL;
PFN_FltStartFiltering gFltStartFiltering = NULL;
PFN_FltGetFileNameInformation gFltGetFileNameInformation = NULL;
PFN_FltReleaseFileNameInformation gFltReleaseFileNameInformation = NULL;
PFN_FltGetRequestorProcessId gFltGetRequestorProcessId = NULL;
PFN_FltGetRoutineAddress gFltGetRoutineAddress = NULL;

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
    { IRP_MJ_WRITE,
      0,
      PreOperation,
      NULL },
      // ▲ 파일 데이터 쓰기 IRP

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

extern "C" NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    auto EncFltGetRoutineAddress = skCrypt("FltGetRoutineAddress");
    gFltGetRoutineAddress = (PFN_FltGetRoutineAddress)FltGetRoutineAddress(EncFltGetRoutineAddress.decrypt());
    EncFltGetRoutineAddress.clear();
    if (!gFltGetRoutineAddress) return STATUS_UNSUCCESSFUL;

    auto EncFltRegisterFilter = skCrypt("FltRegisterFilter");
    gFltRegisterFilter = (PFN_FltRegisterFilter)gFltGetRoutineAddress(EncFltRegisterFilter.decrypt());
    EncFltRegisterFilter.clear();
    if (!gFltRegisterFilter) return STATUS_UNSUCCESSFUL;

    auto EncFltUnregisterFilter = skCrypt("FltUnregisterFilter");
    gFltUnregisterFilter = (PFN_FltUnregisterFilter)gFltGetRoutineAddress(EncFltUnregisterFilter.decrypt());
    EncFltUnregisterFilter.clear();
    if (!gFltUnregisterFilter) return STATUS_UNSUCCESSFUL;

    auto EncFltStartFiltering = skCrypt("FltStartFiltering");
    gFltStartFiltering = (PFN_FltStartFiltering)gFltGetRoutineAddress(EncFltStartFiltering.decrypt());
    EncFltStartFiltering.clear();
    if (!gFltStartFiltering) return STATUS_UNSUCCESSFUL;

    auto EncFltGetFileNameInformation = skCrypt("FltGetFileNameInformation");
    gFltGetFileNameInformation = (PFN_FltGetFileNameInformation)gFltGetRoutineAddress(EncFltGetFileNameInformation.decrypt());
    EncFltGetFileNameInformation.clear();
    if (!gFltGetFileNameInformation) return STATUS_UNSUCCESSFUL;

    auto EncFltReleaseFileNameInformation = skCrypt("FltReleaseFileNameInformation");
    gFltReleaseFileNameInformation = (PFN_FltReleaseFileNameInformation)gFltGetRoutineAddress(EncFltReleaseFileNameInformation.decrypt());
    EncFltReleaseFileNameInformation.clear();
    if (!gFltReleaseFileNameInformation) return STATUS_UNSUCCESSFUL;

    auto EncFltGetRequestorProcessId = skCrypt("FltGetRequestorProcessId");
    gFltGetRequestorProcessId = (PFN_FltGetRequestorProcessId)gFltGetRoutineAddress(EncFltGetRequestorProcessId.decrypt());
    EncFltGetRequestorProcessId.clear();
    if (!gFltGetRequestorProcessId) return STATUS_UNSUCCESSFUL;


    NTSTATUS status;
    status = gFltRegisterFilter(DriverObject,
                                &FilterRegistration,
                                &gFilterHandle );

    if (NT_SUCCESS(status)) {
        status = gFltStartFiltering(gFilterHandle);
        if (!NT_SUCCESS(status)) {
            gFltUnregisterFilter(gFilterHandle);
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

    gFltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}


/*************************************************************************
    ETC...
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

    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    } // IRQL 검사 (PASSIVE_LEVEL이 아닌 상태에서 Paged-Pool 메모리에 접근하지 못합니다)

    NTSTATUS status;

    switch (Data->Iopb->MajorFunction) {
        case IRP_MJ_WRITE: {
            PFLT_FILE_NAME_INFORMATION pFileInfo = NULL;

            status = gFltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED, &pFileInfo);
            if ((!NT_SUCCESS(status)) || (pFileInfo == NULL)) {
                return FLT_PREOP_SUCCESS_NO_CALLBACK;
            } // 파일 경로를 알 수 없으면 해당 IRP를 허용합니다. (PostOperation 미수행)


            auto DiskStr1 = skCrypt(L"\\DEVICE\\HARDDISK*\\DR*");
            UNICODE_STRING CmpDiskPath1;
            RtlInitUnicodeString(&CmpDiskPath1, DiskStr1.decrypt()); 
            BOOLEAN result = FsRtlIsNameInExpression(&CmpDiskPath1, &pFileInfo->Name, TRUE, NULL);
            DiskStr1.clear();
            // "\\DEVICE\\HARDDISK*\\DR*" 경로를 비교

            if (!result) {
                auto DiskStr2 = skCrypt(L"\\DEVICE\\HARDDISK*\\PARTITION0");
                UNICODE_STRING CmpDiskPath2;
                RtlInitUnicodeString(&CmpDiskPath2, DiskStr2.decrypt());
                result = FsRtlIsNameInExpression(&CmpDiskPath2, &pFileInfo->Name, TRUE, NULL);
                DiskStr2.clear();
            } // "\\DEVICE\\HARDDISK*\\PARTITION0" 경로를 비교

            gFltReleaseFileNameInformation(pFileInfo);
            // 파일 이름 할당 해제

            if ((result) && (Data->Iopb->Parameters.Write.ByteOffset.QuadPart <= 512)) {
                // 위 경로에서 512 바이트 오프셋 내부에 접근한다면, MBR 접근으로 간주 
                ULONG AccessPID = gFltGetRequestorProcessId(Data);
                auto strLogDetect = skCrypt("[MINIFILTER] MBR Write Detection. PID(%lu) Path(%wZ)\n");
                DbgPrint(strLogDetect.decrypt(), (ULONG)(ULONG_PTR)AccessPID, &pFileInfo->Name);
                strLogDetect.clear();

                //if (NT_SUCCESS(status)) {
                //    if (TerminateProcess((HANDLE)(ULONG_PTR)AccessPID)) {
                //        DbgPrint("[MINIFILTER] Success to terminate process. (%lu)\n", (ULONG)(ULONG_PTR)AccessPID);
                //    } else {
                //        DbgPrint("[MINIFILTER] Failed to terminate process. (%lu)\n", (ULONG)(ULONG_PTR)AccessPID);
                //    } // EPROCESS 객체를 이용해 프로세스를 종료시킵니다.
                //} // PID를 통해 EPROCESS 객체를 할당받습니다.

                Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                Data->IoStatus.Information = 0;
                return FLT_PREOP_COMPLETE;
                // MBR 접근으로 간주되면, IRP를 거부합니다.
            }
            break;
        }
    }
 
    return FLT_PREOP_SUCCESS_NO_CALLBACK;
    // 위 조건에 해당하지 않으면 IRP를 허용합니다.
}