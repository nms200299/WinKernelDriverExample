# IRP Dispatch Hooking을 이용한 IOCTL 후킹/무력화

* 작성자 : 2N(nms200299)

* 설명 : 테스트 드라이버의 DRIVER_OBJECT에서 IRP Dispatch Routine을 변조하여,
IOCTL 메시지를 변조하고, 무력화하는 기능을 구현하였습니다.

* 테스트 환경 : Hyper-V / Windows 10 22H2 x64 (19045.5965)
