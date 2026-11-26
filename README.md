# Poe Trade Search

Path of Exile 아이템 위에서 Ctrl+C 하면 커서 옆에 팝업이 떠서 시세를 바로 보여주는 Windows 도구.
[phiDelPark/PoeTradeSearch](https://github.com/phiDelPark/PoeTradeSearch)를 C++ / Qt 6로 다시 만들었음.

## 기능

- PoE1 / PoE2, 한국어(카카오) / 영어(글로벌) 클라이언트 지원
- 아이템 옵션을 거래소 검색 조건으로 자동 변환 (수치 완화, pseudo 합산, 프리셋)
- 시세 추정가 + 매물 목록. 화폐·조각처럼 교환 가능한 템은 일괄 교환 시세로 보여줌
- 직전 아이템과 비교, "거래소에서 찾기"로 브라우저 열기
- 채팅 매크로: F2 `/hideout`, F4 `/exit`
- 거래소 요청 제한 지킴 (연타해도 IP 차단 안 먹음)

## 설치

[Releases](https://github.com/j0n9m1n1/poe-trade-search/releases)에서 받으면 됨.

- `PoeTradeSearch-Setup.exe`: 설치 파일 (권장). 관리자 권한 없이 깔림
- `PoeTradeSearch-win64.zip`: 포터블. 아무 폴더에 풀고 `PoeTradeSearch.exe` 실행

처음 실행할 때 Windows SmartScreen 경고가 뜰 수 있음. 코드 서명이 안 돼 있어서 그런 거고 "추가 정보 → 실행" 누르면 됨.

## 사용법

1. 프로그램을 켜면 트레이에 아이콘이 생김
2. 게임에서 아이템에 마우스를 올리고 Ctrl+C
3. 팝업에서 시세 확인. 창 바깥을 클릭하면 닫히고, 트레이 아이콘을 누르면 다시 열림

단축키와 옵션은 트레이 아이콘 우클릭 → 설정에서 바꿈.
게임을 관리자 권한으로 실행했으면 이 프로그램도 관리자로 실행해야 단축키가 먹음.

## 업데이트

새 버전이 나오면 트레이 알림이 뜨고, 트레이 메뉴 "업데이트 설치"로 바로 업데이트됨.
설정에서 자동 설치나 확인 안 함으로 바꿀 수 있음. 설정과 캐시(`data\` 폴더)는 업데이트해도 그대로 남음.

## 빌드

Qt 6.5+, CMake 3.21+ 필요.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
```

테스트, 릴리즈 방법, 코드 구조는 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)에 있음.

## 라이선스

GPL-3.0. 원본이 GPL-3.0이라 같은 라이선스를 씀.

- 시세 데이터: GGG / 카카오 공식 거래소 API
- 옵션 매칭 데이터: [Awakened PoE Trade](https://github.com/SnosMe/awakened-poe-trade), [Exiled Exchange 2](https://github.com/Kvan7/Exiled-Exchange-2) (MIT)

이 프로젝트는 Grinding Gear Games나 Kakao Games와 관계없음.
