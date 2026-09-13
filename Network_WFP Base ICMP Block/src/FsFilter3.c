/*++

Module Name:

   .c

Abstract:

    WFP를 통한 ICMP 패킷 차단 예제

Environment:

    Kernel mode

--*/

#define NDIS_SUPPORT_NDIS6 1
#define NDIS60 1

#include <ntddk.h>
#include <ndis.h> // NET_BUFFER_LIST 정의
#include <fwpsk.h>
#include <initguid.h> // DEFINE_GUID 정의
#include <fwpmk.h>

/*************************************************************************
함수 선언
*************************************************************************/

EXTERN_C_START
DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject);

NTSTATUS InitWfp(PDEVICE_OBJECT WfpDeviceObject);

NTSTATUS FwpsCalloutNotifyFn(
    FWPS_CALLOUT_NOTIFY_TYPE notifyType,
    const GUID* filterKey,
    FWPS_FILTER* filter
);

void FwpsCalloutClassifyFn(
    const FWPS_INCOMING_VALUES *inFixedValues,
    const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
    void* layerData,
    const void* classifyContext,
    const FWPS_FILTER* filter,
    UINT64 flowContext,
    FWPS_CLASSIFY_OUT* classifyOut
);

EXTERN_C_END


/*************************************************************************
전역 변수 및 매크로 상수 선언
*************************************************************************/

PDEVICE_OBJECT g_pWfpDeviceObject = NULL;
HANDLE g_hWfpHandle = NULL;
UINT32 g_dwFwpsCalloutId = 0;
UINT32 g_dwFwpmCalloutId = 0;
UINT64 g_dwFwpmFilterId = 0;

DEFINE_GUID(WFP_SUBLAYER_KEY, 0x11111111, 0x2222, 0x3333,
    0x33, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44);
// WFP_SUBLAYER

DEFINE_GUID(WFP_PROVIDER_KEY, 0x11111111, 0x2222, 0x3333, 
    0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44);
// WFP_PROVIDER_KEY

DEFINE_GUID(WFP_CALLOUT_KEY, 0x11111111, 0x2222, 0x3333, 
    0x55, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44);
// WFP_CALLOUT_KEY



/*************************************************************************
함수 정의
*************************************************************************/
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS Status = STATUS_SUCCESS;

    DbgPrint("[DRIVER] Driver Load.\n");
    DriverObject->DriverUnload = UnloadDriver;
    // 드라이버 언로드 콜백 지정


    UNICODE_STRING WfpDeviceName = RTL_CONSTANT_STRING(L"\\Device\\WFP_TEST");
    // WFP 디바이스 오브젝트 이름

    Status = IoCreateDevice(
        DriverObject,
        0,
        &WfpDeviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &g_pWfpDeviceObject
    ); // WFP 콜아웃으로 사용할 디바이스 오브젝트 생성

    if (NT_SUCCESS(InitWfp(g_pWfpDeviceObject))) {
        // WFP 필터 등록
        DbgPrint("[DRIVER] InitWfp Successed\n");
    } else {
        DbgPrint("[DRIVER] InitWfp Failed..\n");
    }

    return Status;
}

VOID UnloadDriver(IN PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);

    if (g_dwFwpmFilterId && g_hWfpHandle){
        FwpmFilterDeleteById(g_hWfpHandle, g_dwFwpmFilterId);
        g_dwFwpmFilterId = 0;
    } // 추가한 필터 해제
    if (g_dwFwpmCalloutId && g_hWfpHandle){
        FwpmCalloutDeleteById(g_hWfpHandle, g_dwFwpmCalloutId);
        g_dwFwpmCalloutId = 0;
    } // 추가한 FWPM 콜아웃 해제
    if (g_dwFwpsCalloutId != 0){
        FwpsCalloutUnregisterById(g_dwFwpsCalloutId);
        g_dwFwpsCalloutId = 0;
    } // 추가한 FWPS 콜아웃 해제
    if (g_hWfpHandle) {
        FwpmSubLayerDeleteByKey(g_hWfpHandle, &WFP_SUBLAYER_KEY);
        FwpmProviderDeleteByKey(g_hWfpHandle, &WFP_PROVIDER_KEY);
        FwpmEngineClose(g_hWfpHandle);
        g_hWfpHandle = NULL;
    } // 서브레이어, 프로바이더, 엔진 핸들 해제

    if (g_pWfpDeviceObject) {
        IoDeleteDevice(g_pWfpDeviceObject);
        g_pWfpDeviceObject = NULL;
    } // 디바이스 오브젝트 해제

    DbgPrint("[DRIVER] Driver Unload.\n");
}

NTSTATUS InitWfp(PDEVICE_OBJECT WfpDeviceObject) {
    NTSTATUS Status = STATUS_SUCCESS;
    Status = FwpmEngineOpen(NULL, RPC_C_AUTHN_DEFAULT, NULL, NULL, &g_hWfpHandle);
    if (!NT_SUCCESS(Status)) {
        g_hWfpHandle = NULL;
        DbgPrint("[DRIVER] FwpmEngineOpen Failed (0x%X)\n", Status);
        return Status;
    } // WFP 엔진 핸들 발급

    Status = FwpmTransactionBegin(g_hWfpHandle, 0);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] FwpmTransactionBegin Failed (0x%X)\n", Status);
        return Status;
    } // WFP 트랜잭션 시작

    FWPM_PROVIDER FwpmProvider = { 0, };
    RtlZeroMemory(&FwpmProvider, sizeof(FwpmProvider));
    FwpmProvider.serviceName             = (wchar_t*)L"WFP Example";
    FwpmProvider.displayData.name        = (wchar_t*)L"WFP Example Driver";
    FwpmProvider.displayData.description = (wchar_t*)L"The provider object for Example";
    FwpmProvider.providerKey             = WFP_PROVIDER_KEY;
    Status = FwpmProviderAdd(g_hWfpHandle, &FwpmProvider, NULL);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] FwpmProviderAdd Failed (0x%X)\n", Status);
        goto TransactionAbort;
    } // 프로바이더 등록

    FWPM_SUBLAYER FwpmSubLayer = { 0 };
    FwpmSubLayer.subLayerKey = WFP_SUBLAYER_KEY;
    FwpmSubLayer.displayData.name = L"WFP Example Sublayer";
    FwpmSubLayer.providerKey = (GUID *)&WFP_PROVIDER_KEY;
    FwpmSubLayer.weight = 0xFFFF; // 최대 weight
    Status = FwpmSubLayerAdd(g_hWfpHandle, &FwpmSubLayer, NULL);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] FwpmSubLayerAdd Failed (0x%X)\n", Status);
        goto TransactionAbort;
    } // 서브레이어 등록

    FWPS_CALLOUT FwpsCallout = { 0, };
    RtlZeroMemory(&FwpsCallout, sizeof(FwpsCallout));
    FwpsCallout.notifyFn = FwpsCalloutNotifyFn;
    FwpsCallout.classifyFn = FwpsCalloutClassifyFn;
    FwpsCallout.calloutKey = WFP_CALLOUT_KEY;
    Status = FwpsCalloutRegister(WfpDeviceObject, &FwpsCallout, &g_dwFwpsCalloutId);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] FwpsCalloutRegister Failed (0x%X)\n", Status);
        goto TransactionAbort;
    } // FWPS 콜아웃 등록

    FWPM_CALLOUT FwpmCallout = { 0, };
    RtlZeroMemory(&FwpmCallout, sizeof(FwpmCallout));
    FwpmCallout.displayData.name = L"WFP ICMP Block Example";
    FwpmCallout.displayData.description = L"WFP Example Driver";
    FwpmCallout.providerKey = (GUID *)&WFP_PROVIDER_KEY;
    FwpmCallout.calloutKey = WFP_CALLOUT_KEY;
    FwpmCallout.applicableLayer = FWPM_LAYER_INBOUND_TRANSPORT_V4;
    Status = FwpmCalloutAdd(g_hWfpHandle, &FwpmCallout, NULL, &g_dwFwpmCalloutId);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] FwpmCalloutAdd Failed (0x%X)\n", Status);
        goto TransactionAbort;
    } // FWPM 콜아웃 등록

    FWPM_FILTER  FwpmFilter  = { 0, };
    RtlZeroMemory(&FwpmFilter, sizeof(FwpmFilter));
    FwpmFilter.displayData.name        = (wchar_t*)L"WFP ICMP Block Filter";
    FwpmFilter.displayData.description = (wchar_t*)L"The filter object for wfp example";
    FwpmFilter.layerKey                = FWPM_LAYER_INBOUND_TRANSPORT_V4;
    FwpmFilter.subLayerKey             = WFP_SUBLAYER_KEY;
    FwpmFilter.action.type             = FWP_ACTION_CALLOUT_TERMINATING; // 콜아웃에서 판정 여부 결정
    FwpmFilter.action.calloutKey       = WFP_CALLOUT_KEY;
    UINT64 WeightValue = 0xFFFFFFFFFFFFFFFFULL; // 최대 weight
    FwpmFilter.weight.type = FWP_UINT64; // FWP_EMPTY로 설정하면 자동으로 결정함
    FwpmFilter.weight.uint64 = &WeightValue;
    Status = FwpmFilterAdd(g_hWfpHandle, &FwpmFilter, NULL, &g_dwFwpmFilterId);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] FwpmFilterAdd Failed (0x%X)\n", Status);
        goto TransactionAbort;
    } // 서브레이어에 필터를 추가

    Status = FwpmTransactionCommit(g_hWfpHandle);
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[DRIVER] FwpmTransactionCommit Failed (0x%X)\n", Status);
    TransactionAbort:
        if (g_hWfpHandle) FwpmTransactionAbort(g_hWfpHandle);
    } // 트랜잭션을 최종 반영

    return Status;
}

// 콜아웃 등록/해제 알림 함수
NTSTATUS FwpsCalloutNotifyFn(
    FWPS_CALLOUT_NOTIFY_TYPE notifyType,
    const GUID* filterKey,
    FWPS_FILTER* filter
) {
    UNREFERENCED_PARAMETER(notifyType);
    UNREFERENCED_PARAMETER(filterKey);
    UNREFERENCED_PARAMETER(filter);
    DbgPrint("[DRIVER] FwpsCalloutNotifyFn Enter\n");
    return STATUS_SUCCESS;
}


// 네트워크 이벤트 필터링 결정 함수
void FwpsCalloutClassifyFn(
    const FWPS_INCOMING_VALUES *inFixedValues,
    const FWPS_INCOMING_METADATA_VALUES *inMetaValues,
    void* layerData,
    const void* classifyContext,
    const FWPS_FILTER* filter,
    UINT64 flowContext,
    FWPS_CLASSIFY_OUT* classifyOut
) {
    UNREFERENCED_PARAMETER(inFixedValues);
    UNREFERENCED_PARAMETER(inMetaValues);
    UNREFERENCED_PARAMETER(layerData);
    UNREFERENCED_PARAMETER(classifyContext);
    UNREFERENCED_PARAMETER(filter);
    UNREFERENCED_PARAMETER(flowContext);
    UNREFERENCED_PARAMETER(classifyOut);

    if ((classifyOut->rights & FWPS_RIGHT_ACTION_WRITE) == 0){
        return;
    }  // 앞단 필터에서 필터링 결정을 수정하지 못하게 한 경우 리턴
    classifyOut->actionType = FWP_ACTION_PERMIT;
    // 기본 정책은 패킷 허용으로 지정

    if (inFixedValues->layerId != FWPS_LAYER_INBOUND_TRANSPORT_V4){
        return;
    } // INBOUND_TRANSPORT_V4 레이어가 아니면 리턴

    if (inFixedValues->incomingValue[FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_PROTOCOL
        ].value.uint8 != IPPROTO_ICMP){
        return;
    } // ICMP 프로토콜이 아니면 리턴

    classifyOut->actionType = FWP_ACTION_BLOCK;
    // ICMP 프로토콜은 패킷 차단 설정
    classifyOut->rights = classifyOut->rights & ~FWPS_RIGHT_ACTION_WRITE;
    // 뒷단 필터에서 결과를 수정하지 못하게 Hard Block으로 지정

    UINT32 LocalIp = RtlUlongByteSwap(inFixedValues->incomingValue[
        FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_LOCAL_ADDRESS].value.uint32);
    UINT32 RemoteIp = RtlUlongByteSwap(inFixedValues->incomingValue[
        FWPS_FIELD_INBOUND_TRANSPORT_V4_IP_REMOTE_ADDRESS].value.uint32);
    // 출발지 IP와 목적지 IP를 구하고 HBO(리틀 엔디언)으로 변환

    UINT32 Pid = 0;
    if (FWPS_IS_METADATA_FIELD_PRESENT(inMetaValues, FWPS_METADATA_FIELD_PROCESS_ID)) {
        Pid = (UINT32)inMetaValues->processId;
    }
    if (Pid == 0) {
        Pid = PtrToUint(PsGetCurrentProcessId());
    } // PID를 구함

    DbgPrint(
        "[DRIVER][%d] Inbound ICMP Packet Block "
        "(%d.%d.%d.%d -> %d.%d.%d.%d)\n",
        Pid,
        RemoteIp & 0xFF,
        (RemoteIp >> 8)  & 0xFF,
        (RemoteIp >> 16) & 0xFF,
        (RemoteIp >> 24) & 0xFF,
        LocalIp & 0xFF,
        (LocalIp >> 8)  & 0xFF,
        (LocalIp >> 16) & 0xFF,
        (LocalIp >> 24) & 0xFF        
    ); // 각 IP 필드를 파싱하여 출력

    return;
}
