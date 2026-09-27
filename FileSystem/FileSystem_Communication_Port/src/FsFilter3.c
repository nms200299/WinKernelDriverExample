/*++
Abstract:
    커뮤니케이션 포트를 통한 커널 드라이버 언로드 제어

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
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);
NTSTATUS UnloadDriver(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
);
NTSTATUS FltConnectCallback(
    PFLT_PORT ClientPort,
    PVOID ServerPortCookie,
    PVOID ConnectionContext,
    ULONG SizeOfContext,
    PVOID* ConnectionPortCookie
);
VOID FltDisconnectCallback(
    PVOID ConnectionCookie
);
NTSTATUS FltMessageCallback(
    PVOID PortCookie,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength,
    PULONG ReturnOutputBufferLength
);
EXTERN_C_END


/*************************************************************************
    UserDefined ...
*************************************************************************/

#define PORT_NAME			L"\\TEST_PORT"
#define MAX_MESSAGE_LEN     1024

typedef struct _CommStruct {
    UINT8 Type;
    WCHAR szMessage[MAX_MESSAGE_LEN];
} CommStruct, * PCommStruct;

PFLT_PORT g_pFltServerPort = NULL;
PFLT_PORT g_pFltClientPort = NULL;
BOOLEAN g_bUnloadFlag = TRUE;

/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

CONST FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags
    NULL,                               //  Context
    NULL,                               //  Operation callbacks
    UnloadDriver,                               //  MiniFilterUnload 
    NULL,                               //  InstanceSetup
    NULL,                               //  InstanceQueryTeardown
    NULL,                               //  InstanceTeardownStart
    NULL,                               //  InstanceTeardownComplete
    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent
};


NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
) {
    NTSTATUS Status;
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("[DRIVER] %s : Driver Load.\n", __func__);

    Status = FltRegisterFilter(DriverObject,
        &FilterRegistration,
        &gFilterHandle);
    if (!NT_SUCCESS(Status)) return Status;
    // 필터 핸들 등록

    PSECURITY_DESCRIPTOR pSd = NULL;
    Status = FltBuildDefaultSecurityDescriptor(&pSd, FLT_PORT_ALL_ACCESS);
    if (!NT_SUCCESS(Status)) goto FltUnReg;
    // SecurityDescriptor 설정

    UNICODE_STRING uniPortName;
    RtlInitUnicodeString(&uniPortName, PORT_NAME);
    OBJECT_ATTRIBUTES Oa = { 0 };
    RtlZeroMemory(&Oa, sizeof(Oa));
    InitializeObjectAttributes(&Oa, &uniPortName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, pSd);
    // Object Attribute 설정

    Status = FltCreateCommunicationPort(gFilterHandle,
        &g_pFltServerPort,
        &Oa,
        NULL,
        FltConnectCallback,
        FltDisconnectCallback,
        FltMessageCallback,
        1); // MaxConnections
    // 커뮤니케이션 포트 등록

    FltFreeSecurityDescriptor(pSd);
    // pSd 할당 해제

    if (!NT_SUCCESS(Status)) {
    FltUnReg:
        FltUnregisterFilter(gFilterHandle);
        gFilterHandle = NULL;
        return Status;
    }

    return Status;
}

NTSTATUS UnloadDriver(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
) {
    UNREFERENCED_PARAMETER(Flags);

    if ((g_pFltClientPort) && (gFilterHandle)) {
        CommStruct SendInfo, RepInfo;
        //CommRepStruct RepInfo;
        RtlZeroMemory(&SendInfo, sizeof(CommStruct));
        RtlZeroMemory(&RepInfo, sizeof(CommStruct));

        SendInfo.Type = 2;
        wcscpy_s(SendInfo.szMessage, ARRAYSIZE(SendInfo.szMessage), L"Driver Unload Request");

        NTSTATUS Status = STATUS_SUCCESS;
        ULONG ReplyLength = sizeof(CommStruct);
        Status = FltSendMessage(gFilterHandle,
            &g_pFltClientPort,
            &SendInfo,
            sizeof(CommStruct),
            &RepInfo,
            &ReplyLength,
            NULL);
        // 클라이언트에게 메시지 전송

        DbgPrint("[DRIVER] %s : Msg Send = %ls (%llu)\n",
            __func__,
            RepInfo.szMessage,
            (ULONG64)(ULONG_PTR)PsGetCurrentProcessId());

        if (NT_SUCCESS(Status)) {
            if (RepInfo.Type == 2) {
                if (RepInfo.szMessage[0] == L'0') {
                    g_bUnloadFlag = FALSE;
                } else {
                    g_bUnloadFlag = TRUE;
                } // 드라이버 언로드 플래그 설정

                DbgPrint("[DRIVER] %s : Driver UnloadFlag Set %d\n",
                    __func__,
                    g_bUnloadFlag);
            }
        } else {
            DbgPrint(
                "FltSendMessage Status = 0x%08X, ReplyLength = %lu\n",
                Status,
                ReplyLength
            );

            g_bUnloadFlag = TRUE;
            // 클라이언트 종료 등으로 FltSendMessage가 실패하면 드라이버 언로드 가능
        }

        if (!g_bUnloadFlag) {
            DbgPrint("[DRIVER] %s : Driver Unload Denied.\n", __func__);
            return STATUS_ACCESS_DENIED;
        }
        // g_bUnloadFlag가 False면 드라이버 언로드 방지

        FltCloseClientPort(gFilterHandle, &g_pFltClientPort);
        g_pFltClientPort = NULL;
        // 클라이언트 포트 종료
    }

    if (g_pFltServerPort) {
        FltCloseCommunicationPort(g_pFltServerPort);
        g_pFltServerPort = NULL;
    } // 서버 포트 종료

    if (!gFilterHandle) return STATUS_SUCCESS;
    FltUnregisterFilter(gFilterHandle);
    gFilterHandle = NULL;
    DbgPrint("[DRIVER] %s : Driver Unload.\n", __func__);
    // 필터 등록 해제

    return STATUS_SUCCESS;
}

/*************************************************************************
    MiniFilter Communication routines.
*************************************************************************/

NTSTATUS FltConnectCallback(
    PFLT_PORT ClientPort,
    PVOID ServerPortCookie,
    PVOID ConnectionContext,
    ULONG SizeOfContext,
    PVOID* ConnectionPortCookie
) {
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionPortCookie);

    DbgPrint(
        "[DRIVER] %s : Connected Client (%llu)\n",
        __func__,
        (ULONG64)(ULONG_PTR)PsGetCurrentProcessId());
    if (!g_pFltClientPort) {
        g_pFltClientPort = ClientPort;
    }

    return STATUS_SUCCESS;
}

VOID FltDisconnectCallback(
    PVOID ConnectionCookie
) {
    UNREFERENCED_PARAMETER(ConnectionCookie);
    DbgPrint(
        "[DRIVER] %s : Disconnected Client (%llu)\n",
        __func__,
        (ULONG64)(ULONG_PTR)PsGetCurrentProcessId());
    if (g_pFltClientPort) {
        g_pFltClientPort = NULL;
    }
}


NTSTATUS FltMessageCallback(
    PVOID PortCookie,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength,
    PULONG ReturnOutputBufferLength
) {
    UNREFERENCED_PARAMETER(PortCookie);

    if (InputBufferLength != sizeof(CommStruct)) return STATUS_SUCCESS;
    if (!InputBuffer) return STATUS_SUCCESS;
    CommStruct* ReciveInfo = (CommStruct*)InputBuffer;
    // 입력 구조체 검증

    switch (ReciveInfo->Type) {
    case 1: {
        // 클라이언트 요청에 대한 드라이버의 응답 처리  
        DbgPrint("[DRIVER] %s : Msg Recive = %ls (%llu)\n",
            __func__,
            ReciveInfo->szMessage,
            (ULONG64)(ULONG_PTR)PsGetCurrentProcessId());

        if (OutputBufferLength != sizeof(CommStruct)) return STATUS_SUCCESS;
        if (!OutputBuffer) return STATUS_SUCCESS;
        CommStruct* SendInfo = (CommStruct*)OutputBuffer;
        RtlZeroMemory(SendInfo, sizeof(CommStruct));
        SendInfo->Type = 1;
        wcscpy_s(SendInfo->szMessage, ARRAYSIZE(SendInfo->szMessage), L"Hello, User-Application");
        DbgPrint("[DRIVER] %s : Msg Send = %ls (%llu)\n",
            __func__,
            SendInfo->szMessage,
            (ULONG64)(ULONG_PTR)PsGetCurrentProcessId());
        *ReturnOutputBufferLength = sizeof(CommStruct);
        return STATUS_SUCCESS;
        break;
    }
    }
    return STATUS_SUCCESS;
}
