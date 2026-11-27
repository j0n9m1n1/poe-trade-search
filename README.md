# Poe Trade Search

Path of Exile 아이템 위에서 Alt+D(또는 Ctrl+C)를 누르면 커서 옆에 팝업을 띄워 시세를 바로 보여 주는 Windows 도구입니다.
[phiDelPark/PoeTradeSearch](https://github.com/phiDelPark/PoeTradeSearch)를 C++ / Qt 6로 다시 만들었습니다.

## 기능

- PoE1 / PoE2, 한국어(카카오) / 영어(글로벌) 클라이언트를 지원합니다.
- 아이템 옵션을 거래소 검색 조건으로 자동 변환합니다 (수치 완화, pseudo 합산, 프리셋, 타락·두 번 타락·삿된).
- 시세 추정가와 매물 목록을 한 화면에 보여 줍니다. 화폐·조각처럼 교환 가능한 아이템은 일괄 교환 시세를 보여 줍니다.
- 방어도·회피·에너지 보호막·DPS 같은 아이템 속성도 검색 조건으로 쓸 수 있습니다 (거래소처럼 퀄리티 20% 기준).
- 옵션은 게임 글씨 색과 접두/접미 등급(P1/S1)으로 표시하고, 글자를 눌러 검색에 넣거나 뺍니다.
- 기본으로 즉시 구입 매물 중 카오스 오브/신성한 오브(PoE2는 엑잘티드 오브/신성한 오브)로 가격을 올린 매물만 봅니다. 색채의 오브 같은 다른 화폐로 올린 매물은 제외되며, 설정에서 바꿀 수 있습니다.
- "거래소에서 찾기"로 브라우저에서 바로 열 수 있습니다.
- 채팅 매크로: F2 `/hideout`, F4 `/exit`
- 거래소 요청 제한을 지키므로 연속으로 검색해도 IP가 차단되지 않습니다.

## 설치

[Releases](https://github.com/j0n9m1n1/poe-trade-search/releases)에서 받을 수 있습니다.

- `PoeTradeSearch-Setup.exe`: 설치 파일 (권장). 관리자 권한 없이 설치됩니다.
- `PoeTradeSearch-win64.zip`: 포터블 버전. 원하는 폴더에 압축을 풀고 `PoeTradeSearch.exe`를 실행하세요.

처음 실행할 때 Windows SmartScreen 경고가 나타날 수 있습니다. 코드 서명이 되어 있지 않아서 나타나는 경고이며, "추가 정보 → 실행"을 누르면 됩니다.

## 사용법

1. 프로그램을 실행하면 트레이에 아이콘이 생깁니다.
2. 게임에서 아이템에 마우스를 올리고 Alt+D를 누릅니다. 프로그램이 Ctrl+Alt+C(고급 설명 복사)를 대신 눌러 아이템 정보를 가져옵니다. 그냥 Ctrl+C로 복사해도 됩니다.
3. 팝업에서 시세를 확인합니다. 게임 화면을 클릭하면 닫히고(창 모드에서 다른 프로그램을 누르면 닫히지 않습니다), 트레이 아이콘을 누르면 다시 열립니다.

단축키와 옵션은 트레이 아이콘 우클릭 → 설정에서 바꿀 수 있습니다.
게임을 관리자 권한으로 실행했다면 이 프로그램도 관리자 권한으로 실행해야 단축키가 동작합니다.

## 업데이트

새 버전이 나오면 트레이 알림이 표시됩니다. 알림을 누르거나, 트레이 우클릭 메뉴의 "업데이트 확인" 또는 설정 > 프로그램 업데이트의 버튼으로 바로 업데이트할 수 있습니다.
설정에서 자동 설치 또는 확인 안 함으로 바꿀 수 있습니다. 설정과 캐시(`data\` 폴더)는 업데이트해도 그대로 유지됩니다.

## 빌드

Qt 6.5 이상, CMake 3.21 이상이 필요합니다.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
```

테스트, 릴리즈 방법, 코드 구조는 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)를 참고하세요.

## 라이선스

GPL-3.0입니다. 원본이 GPL-3.0이므로 같은 라이선스를 따릅니다.

- 시세 데이터: GGG / 카카오 공식 거래소 API
- 옵션 매칭 데이터: [Awakened PoE Trade](https://github.com/SnosMe/awakened-poe-trade), [Exiled Exchange 2](https://github.com/Kvan7/Exiled-Exchange-2) (MIT)

이 프로젝트는 Grinding Gear Games 및 Kakao Games와 관계가 없습니다.
