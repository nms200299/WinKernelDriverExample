#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <fltUser.h>

// ####################################################

#define PORT_NAME			L"\\TEST_PORT"
#define MAX_MESSAGE_LEN     1024

typedef struct _CommStruct {
    UINT8 Type;
    WCHAR szMessage[MAX_MESSAGE_LEN];
} CommStruct, * PCommStruct;

typedef struct _CommGetStruct {
	FILTER_MESSAGE_HEADER hdr;
	CommStruct data;
} CommGetStruct, *PCommGetStruct;

typedef struct _CommRepStruct {
	FILTER_REPLY_HEADER hdr;
	CommStruct data;
} CommRepStruct, * PCommRepStruct;


// ####################################################

int main() {
	HRESULT hResult;
	HANDLE hPortHandle;

	hResult = FilterConnectCommunicationPort(
		PORT_NAME,
		0,
		NULL,
		0,
		NULL,
		&hPortHandle
	);
	if (hResult != S_OK) {
		printf("FilterConnectCommunicationPort() Fail(%x)\n", hResult);
		return 1;
	} // 커뮤니케이션 포트 연결


	printf("[1. User -> Driver 통신]\n");

	CommStruct SendInfo, ReciveInfo;
	DWORD ReturnBytes = 0;

	SendInfo.Type = 1;
	wcscpy_s(SendInfo.szMessage, MAX_MESSAGE_LEN, L"Hello, Driver");
	hResult = FilterSendMessage(
		hPortHandle,
		&SendInfo,
		sizeof(CommStruct),
		&ReciveInfo,
		sizeof(CommStruct),
		&ReturnBytes
	);
	if (hResult != S_OK) {
		printf("FilterSendMessage() Fail(%x)\n", hResult);
		return 1;
	}
	printf("Msg Send : %ls\n", SendInfo.szMessage);
	// 드라이버한테 메시지 전송

	if (ReturnBytes == sizeof(CommStruct)) {
		printf("Msg Recive : %ls\n", ReciveInfo.szMessage);
	} // 응답 크기가 일치하면 출력

	printf("\n[2. Driver -> User 통신]\n");

	CommGetStruct ReviceGetInfo;
	ZeroMemory(&ReviceGetInfo, sizeof(ReviceGetInfo));
	hResult = FilterGetMessage(hPortHandle, &ReviceGetInfo.hdr, sizeof(ReviceGetInfo), NULL);
	if (hResult != S_OK) {
		printf("FilterGetMessage() Fail(%x)\n", hResult);
		return 1;
	}
	printf("Msg Get : %ls\n", ReviceGetInfo.data.szMessage);
	// 드라이버한테 메시지 수신

	BOOL bAllowFlag = 0;
	printf("Allow driver unloading? (1/0) : ");
	scanf_s("%d", &bAllowFlag);
	printf("\n");

	CommRepStruct ReplyInfo;
	ZeroMemory(&ReplyInfo, sizeof(ReplyInfo));
	ReplyInfo.hdr.MessageId = ReviceGetInfo.hdr.MessageId;
	ReplyInfo.data.Type = 2;
	if (bAllowFlag) {
		wcscpy_s(ReplyInfo.data.szMessage, MAX_MESSAGE_LEN, L"1");
	} else {
		wcscpy_s(ReplyInfo.data.szMessage, MAX_MESSAGE_LEN, L"0");
	}
	hResult = FilterReplyMessage(hPortHandle, &ReplyInfo.hdr, sizeof(ReplyInfo.hdr) + sizeof(ReplyInfo.data));
	if (hResult != S_OK) {
		printf("FilterReplyMessage() Fail(%x)\n", hResult);
		return 1;
	}
	printf("Msg Rep : %ls\n", ReplyInfo.data.szMessage);
	// 수신한 메시지에 대한 회신

	system("pause");
	FilterClose(hPortHandle);
    return 0;
}