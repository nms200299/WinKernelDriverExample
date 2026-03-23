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
  |심볼 노출 비교|||
  |평문 문자열 노출 비교|||
  |복호화된 DriverEntry 코드 비교|||

  * 동작 테스트



