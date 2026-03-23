#pragma once
#include <fltKernel.h>

typedef
NTSTATUS
(*PFN_FltRegisterFilter)(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ CONST FLT_REGISTRATION* Registration,
    _Outptr_ PFLT_FILTER* RetFilter
    );

typedef
VOID
(*PFN_FltUnregisterFilter)(
    _In_ PFLT_FILTER Filter
    );

typedef
NTSTATUS
(*PFN_FltStartFiltering)(
    _In_ PFLT_FILTER Filter
    );

typedef
NTSTATUS
(*PFN_FltGetFileNameInformation)(
    _In_ PFLT_CALLBACK_DATA CallbackData,
    _In_ FLT_FILE_NAME_OPTIONS NameOptions,
    _Outptr_ PFLT_FILE_NAME_INFORMATION* FileNameInformation
    );

typedef
VOID
(*PFN_FltReleaseFileNameInformation)(
    _In_ PFLT_FILE_NAME_INFORMATION FileNameInformation
    );

typedef
ULONG
(*PFN_FltGetRequestorProcessId)(
    _In_ PFLT_CALLBACK_DATA CallbackData
    );

typedef
PVOID
(*PFN_FltGetRoutineAddress)(
    _In_ PCSTR FltMgrRoutineName
);