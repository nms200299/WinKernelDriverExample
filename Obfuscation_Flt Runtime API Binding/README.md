# 런타임 Flt API 동적 바인딩

* 작성자 : 2N(nms200299)

* 설명

  * MBR 보호 드라이버를 스켈레톤 코드로 사용
(https://github.com/nms200299/WinKernelDriverExample/tree/main/FileSystem_MBR_Protect)


  * FltGetRoutineAddress() API로 Flt API를 런타임 동적 바인딩

  * skCrypt 라이브러리로 컴파일 타임 문자열 암호화


* 테스트 도구
  
  * VM 및 OS : Hyper-V / Windows 10 22H2 x64 (19045.5965)

  * 디스어셈블러 : Ghidra 12.02


* 테스트 결과

  * 난독화 테스트

    |항목|난독화 전|난독화 후|
    |---|---|---|
    |심볼 노출 비교|<img width="726" height="367" alt="image (2)" src="https://github.com/user-attachments/assets/213a9adb-efd6-4ec7-a63d-f82d1f9e6e0b" />|<img width="726" height="204" alt="image (3)" src="https://github.com/user-attachments/assets/db61d22c-38ce-4491-8cd6-af861f876f86" />|
    |평문 문자열 노출 비교|<img width="998" height="324" alt="image (4)" src="https://github.com/user-attachments/assets/78dfac10-ef07-445a-b198-fae9efd74b34" />|<img width="1011" height="398" alt="image (5)" src="https://github.com/user-attachments/assets/a865fe31-9142-4a1b-89c8-9134a8ad4419" />|
    |복호화된 DriverEntry 코드 비교|<img width="514" height="753" alt="image (6)" src="https://github.com/user-attachments/assets/b37d437a-ccdb-48b5-af5f-547a9e0dc3a8" />|<img width="538" height="752" alt="image (7)" src="https://github.com/user-attachments/assets/f1021fc6-380c-453b-98b7-64927264e4bd" />|

  * 동작 테스트

    https://github.com/user-attachments/assets/5be72d31-100b-47ef-b960-b7ebc9ae1df4

