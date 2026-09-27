#include <stdio.h>
#include <windows.h>


int main(){
    printf("[Memory-mapped I/O Test]\n\n");

    wchar_t FileName[255] = { 0, };

    printf("FileName : ");
    wscanf_s(L"%ls", FileName, 255);
    printf("test");
    HANDLE hFile = CreateFile(
        FileName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) printf("CreateFile Fail! \n");
    else  printf("CreateFile Success! \n");

    LARGE_INTEGER size;
    size.QuadPart = 1024;
    HANDLE hMap = CreateFileMapping(
        hFile,
        NULL,
        PAGE_READWRITE,
        size.HighPart,
        size.LowPart,
        NULL
    ); // 최대 1KB까지 확장되는 Map 오브젝트를 생성
    if (hMap == INVALID_HANDLE_VALUE) printf("CreateFile Fail! \n");
    else  printf("CreateFile Success! \n");

    BYTE* view = (BYTE*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0);
    // View에 파일 전체를 매핑
    if (view == NULL) printf("MapViewOfFile Fail! \n");
    else {
        printf("MapViewOfFile Success! \n");
        memcpy(view, "HELLO", 5);
        // 데이터 기록
    }


    if (FlushViewOfFile(view, 0)) printf("FlushViewOfFile Success! \n");
    else printf("FlushViewOfFile Fail! \n");
    
    if (FlushFileBuffers(hFile)) printf("FlushFileBuffers Success! \n");
    else printf("FlushFileBuffers Fail! \n");
    // 디스크 반영
    
    if (view != NULL) UnmapViewOfFile(view);
    if (hMap != INVALID_HANDLE_VALUE) CloseHandle(hMap);

    LARGE_INTEGER Resize;
    Resize.QuadPart = 5;
    SetFilePointerEx(hFile, Resize, NULL, FILE_BEGIN);
    SetEndOfFile(hFile);
    // 파일 크기를 실제 쓴 크기만큼 조정 

    CloseHandle(hFile);
    // 할당 해제

    system("pause");
    return 0;

}
