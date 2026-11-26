# POE 거래소 검색 (C++/Qt)

인게임 아이템 위에서 Ctrl+C 하면 팝업이 떠서 거래소 검색 / 시세 확인을 해주는 Windows 도구.
[phiDelPark/PoeTradeSearch](https://github.com/phiDelPark/PoeTradeSearch) (C#/WPF, 3.16 기준)를 C++17 / Qt 6 로 다시 짠 것이고, 현행 PoE1 + PoE2 거래소 API 기준으로 맞춤.

- 라이선스: **GPL-3.0** (원본이 GPL-3.0 이라 파생 저작물도 GPL-3.0). `LICENSE` 참고.
- 원본 대비 바뀐 점 / 뺀 기능 전체 목록: [`docs/FEATURES.md`](docs/FEATURES.md)

## 1차 버전 기능

- PoE1 / PoE2 둘 다. 게임 창 제목(`Path of Exile 2`)으로 자동 구분 (설정에서 고정 가능)
- 한국어 / 영어 클라이언트 둘 다. 한글 아이템은 카카오(`poe.kakaogames.com`), 영문은 글로벌(`www.pathofexile.com`)
- Ctrl+C 클립보드 감시 (PoE 창이 앞에 있을 때만), 또는 `{Run}` 단축키로 Ctrl+Alt+C 주입
- 옵션 → 거래소 stat 자동 매칭 (로컬 옵션, 다중 줄, 성단 주얼 인챈트, 변형 젬, 룬/훼손 옵션 포함)
- pseudo 자동 선택, 저항 합산, 우클릭 자동 체크(`Checked.txt`), Ctrl+Alt+C 등급(T1~T2만 자동 체크)
- 무기 DPS (Q20 보정), 홈/링크, 퀄리티/레벨/지도 등급, 타락, 영향(PoE1), 결합/역병, 강탈, 결전
- 시세 조회 (검색 → 10개씩 fetch, 가격 분포 요약), 일괄 교환 시세 (화폐/조각 등)
- "거래소에서 찾기"로 브라우저 열기, 위키(poewiki / poe2wiki, 한글은 poedb) 열기
- 거래소 레이트 리밋(`X-Rate-Limit-*` 헤더) 준수 — 원본처럼 연타하다 IP 밴 먹는 일 없게
- 거래소 데이터(stats/items/static/filters/leagues) 자동 다운로드·캐시, 지난 리그면 현재 리그로 자동 교체
- 원본 `Config.txt` / `Checked.txt` 그대로 읽음 (데이터 폴더에 넣으면 됨)

1차에서 뺀 것: 채팅 매크로, 이미지 팝업, 창고 좌표 그리드, Ctrl+휠 창고 탭 이동, POESESSID 특수 창고 시세, 자동 업데이트.

## 빌드 (Windows)

필요: Qt 6.5 이상 (MSVC 2022 64bit 또는 MinGW 64bit), CMake 3.21+, Ninja 권장

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\PoeTradeSearch.exe
```

실행하면 `PoeTradeSearch.exe` 옆에 `data\` 폴더가 생기고 거기에 설정/캐시/로그가 들어감 (쓰기 불가 위치면 `%APPDATA%\PoeTradeSearch`).
PoE 를 관리자 권한으로 돌리면 이 프로그램도 관리자로 실행해야 단축키/키 주입이 먹음 (Windows UIPI).

## 개발 / 테스트 (리눅스에서도 코어는 빌드됨)

리눅스에선 플랫폼 계층이 스텁이라 게임 감지/단축키는 안 되고, 파서·쿼리·네트워크·UI 는 그대로 돌아감.

```sh
cmake -S . -B build -G Ninja && cmake --build build

# 거래소 데이터 받기 (data/cache/... 에 저장)
./build/pts_cli --data ./data --update --game poe1 tests/fixtures/poe1_en_rare_ring.txt

# 단위 테스트 (파서 테스트는 실데이터 필요, 없으면 SKIP)
PTS_TEST_DATA=./data ./build/test_core

# 아이템 텍스트 하나 파싱해서 쿼리 보기 / 실제 검색 / 시세
./build/pts_cli --data ./data --league Standard tests/fixtures/poe1_en_rare_sword.txt
./build/pts_cli --data ./data --league Standard --price 20 tests/fixtures/poe1_en_unique.txt

# 화면 없이 팝업 렌더링 확인
QT_QPA_PLATFORM=offscreen PTS_DATA_DIR=./data PTS_DEV_ITEM=tests/fixtures/poe1_ko_rare_ring.txt \
  PTS_DEV_SHOT=/tmp/popup.png ./build/PoeTradeSearch
```

## 구조

```
src/core      파서(ItemParser), 거래소 DB/stat 인덱스(TradeDb), 쿼리(QueryBuilder), 설정, 자동 체크
src/net       레이트 리밋 큐(TradeClient), 데이터 캐시(DataManager), 엔드포인트
src/platform  Windows: 포그라운드 감지 / SendInput / RegisterHotKey / WS_EX_NOACTIVATE (그 외 OS 는 스텁)
src/ui        팝업, 옵션 행, 설정, 단축키 입력
resources     parser.json (한/영 키워드, 아이템 종류 매핑, pseudo 매핑 등)
tools         pts_cli (디버그용)
```

`resources/parser.json` 을 데이터 폴더에 복사해서 고치면 그게 우선 적용됨. 한국어 클라이언트 문구가 틀려서 안 잡히는 게 있으면 여기만 고치면 됨.

## 아직 검증 못 한 것

- 실제 Windows 빌드/실행 (이 저장소는 리눅스에서 개발, Win32 코드는 mingw 로 타입 검사까지만 함)
- 한국어 클라이언트 신규 문구 일부(역병에 유린당한, 삿된, 경로석 등급, PoE2 한글 아이템 종류)는 거래소 필터 텍스트 기준 추정값
- PoE2 클립보드 포맷은 `Requires: Level ...` 한 줄 요구사항 기준으로 판별
- `server_redirect` 모드(`/trade/search/{league}?q=`)
- 다른 언어 서버로 검색(한글 아이템 → 글로벌): 거래소 items 목록 순번으로 매칭해서, 한/영 목록 길이가 다른 그룹은 실패함
